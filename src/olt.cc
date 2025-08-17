/*
 * olt.cc
 *
 *  Created on: 30 July 2025
 *      Author: mondals
 */

#include <string.h>
#include <math.h>
#include <omnetpp.h>
#include <numeric>   // Required for std::iota
#include <algorithm> // Required for std::sort

#include "sim_params.h"
#include "ethPacket_m.h"
#include "ping_m.h"
#include "gtc_header_m.h"

using namespace std;
using namespace omnetpp;

class OLT : public cSimpleModule
{
    private:
        //cQueue olt_queue;
        vector<double> onu_rtt;
        vector<double> onu_buffer_TC1;
        vector<double> onu_buffer_TC2;
        vector<double> onu_buffer_TC3;
        vector<double> onu_grant_TC1;
        vector<double> onu_grant_TC2;
        vector<double> onu_grant_TC3;
        vector<double> onu_start_time_TC1;
        vector<double> onu_start_time_TC2;
        vector<double> onu_start_time_TC3;
        vector<int> onu_index;
        vector<double> onu_tx_start_TC1;
        vector<double> onu_tx_start_TC2;
        vector<double> onu_tx_start_TC3;
        long seqID = 0;

        int onus;
        int ping_count = 0;
        double onu_max_grant;

        //simsignal_t errorSignal;
        simsignal_t latencySignalXr;
        simsignal_t latencySignalHmd;
        simsignal_t latencySignalCtr;
        simsignal_t latencySignalHpt;
        simsignal_t latencySignalBkg;

    public:
        //virtual ~OLT();

    protected:
        double ber;
        long totalBitsReceived = 0;
        long totalPacketsReceived = 0;
        long corruptedPackets = 0;

        // The following redefined virtual function holds the algorithm.
        virtual void initialize() override;
        virtual void handleMessage(cMessage *msg) override;
        //virtual ponPacket *generateGrantPacket();
};

Define_Module(OLT);

void OLT::initialize()
{
    //errorSignal = registerSignal("pkt_error");  // registering the signal
    latencySignalXr = registerSignal("xr_latency");
    latencySignalHmd = registerSignal("hmd_latency");
    latencySignalCtr = registerSignal("ctrl_latency");
    latencySignalHpt = registerSignal("hptc_latency");
    latencySignalBkg = registerSignal("bkg_latency");

    //olt_queue.setName("olt_queue");

    gate("SpltGate_i")->setDeliverImmediately(true);

    onus = par("NumberOfONUs");
    EV << getFullName() <<" No. of ONUs detected = " << onus << endl;

    onu_rtt.resize(onus,0);
    onu_buffer_TC1.resize(onus,0);
    onu_buffer_TC2.resize(onus,0);
    onu_buffer_TC3.resize(onus,0);
    onu_grant_TC1.resize(onus,0);
    onu_grant_TC2.resize(onus,0);
    onu_grant_TC3.resize(onus,0);
    onu_start_time_TC1.resize(onus,0);
    onu_start_time_TC2.resize(onus,0);
    onu_start_time_TC3.resize(onus,0);

    for(int j = 0; j<onus; j++) {
        onu_index.push_back(j);
    }

    //EV << getFullName() <<" onu_rtt[0] = " << onu_rtt[0] << ", onu_rtt[1] = " << onu_rtt[1] << endl;
    // bw_map = [onu_id, tc_type, start_time, grant_size]

    ping *png = new ping("ping");      // sending ping message at T = 0 for finding the RTT of all ONUs
    send(png,"SpltGate_o");
    EV << getFullName() << " Sending ping from OLT at = " << simTime() << endl;
}

void OLT::handleMessage(cMessage *msg)
{
    if(msg->isPacket() == true) {
        if(strcmp(msg->getName(),"gtc_hdr_ul") == 0) {                  // updating buffer size after receiving requests from ONUs
            gtc_header *pkt = check_and_cast<gtc_header *>(msg);

            int onuId = pkt->getOnuID();
            // for T-CONT 2
            onu_buffer_TC2[onuId] = pkt->getBufferOccupancyTC2();
            EV << getFullName() << " updated onu_buffer_TC2[" << onuId << "] = " << onu_buffer_TC2[onuId] << endl;
            // for T-CONT 3
            onu_buffer_TC3[onuId] = pkt->getBufferOccupancyTC3();
            EV << getFullName() <<" updated onu_buffer_TC3[" << onuId << "] = " << onu_buffer_TC3[onuId] << endl;

            delete pkt;         // nothing more to do with the header
        }
        else if(strcmp(msg->getName(),"bkg_data") == 0) {
            ethPacket *pkt = check_and_cast<ethPacket *>(msg);

            int onuId = pkt->getOnuId();
            int sfuId = pkt->getSfuId();
            int mfuId = pkt->getMfuId();
            int tcId = pkt->getTContId();

            if((onuId==0) && (mfuId==0)) {                          // Background from random devices at all SFUs
                double bkg_packet_latency = pkt->getArrivalTime().dbl() - pkt->getGenerationTime().dbl();
                EV << getFullName() << " background packet_latency: " << bkg_packet_latency << endl;
                emit(latencySignalBkg, bkg_packet_latency);
            }
            delete pkt;
        }
        else if(strcmp(msg->getName(),"xr_data") == 0) {
            ethPacket *pkt = check_and_cast<ethPacket *>(msg);

            int onuId = pkt->getOnuId();
            int sfuId = pkt->getSfuId();
            int mfuId = pkt->getMfuId();
            int tcId = pkt->getTContId();

            if((onuId==0) && (mfuId==0) && ((sfuId%2)==0)) {            // XR from robots at odd SFUs
                double xr_packet_latency = pkt->getArrivalTime().dbl() - pkt->getGenerationTime().dbl();
                EV << getFullName() << " XR packet_latency: " << xr_packet_latency << endl;
                emit(latencySignalXr, xr_packet_latency);
            }
            delete pkt;
        }
        else if(strcmp(msg->getName(),"haptic_data") == 0) {
            ethPacket *pkt = check_and_cast<ethPacket *>(msg);

            int onuId = pkt->getOnuId();
            int sfuId = pkt->getSfuId();
            int mfuId = pkt->getMfuId();
            int tcId = pkt->getTContId();

            if((onuId==0) && (mfuId==0) && ((sfuId%2)==0)) {            // Haptics from robots at odd SFUs
                double hptc_packet_latency = pkt->getArrivalTime().dbl() - pkt->getGenerationTime().dbl();
                EV << getFullName() << " Haptic packet_latency: " << hptc_packet_latency << endl;
                emit(latencySignalHpt, hptc_packet_latency);
            }
            delete pkt;
        }
        else if(strcmp(msg->getName(),"hmd_data") == 0) {
            ethPacket *pkt = check_and_cast<ethPacket *>(msg);

            int onuId = pkt->getOnuId();
            int sfuId = pkt->getSfuId();
            int mfuId = pkt->getMfuId();
            int tcId = pkt->getTContId();

            if((onuId==0) && (mfuId==0) && ((sfuId%2)!=0)) {            // HMD from humans at odd SFUs
                double hmd_packet_latency = pkt->getArrivalTime().dbl() - pkt->getGenerationTime().dbl();
                EV << getFullName() << " HMD packet_latency: " << hmd_packet_latency << endl;
                emit(latencySignalHmd, hmd_packet_latency);
            }
            delete pkt;
        }
        else if(strcmp(msg->getName(),"control_data") == 0) {
            ethPacket *pkt = check_and_cast<ethPacket *>(msg);

            int onuId = pkt->getOnuId();
            int sfuId = pkt->getSfuId();
            int mfuId = pkt->getMfuId();
            int tcId = pkt->getTContId();

            if((onuId==0) && (mfuId==0) && ((sfuId%2)!=0)) {            // Control from humans at odd SFUs
                double ctrl_packet_latency = pkt->getArrivalTime().dbl() - pkt->getGenerationTime().dbl();
                EV << getFullName() << " Control packet_latency: " << ctrl_packet_latency << endl;
                emit(latencySignalCtr, ctrl_packet_latency);
            }
            delete pkt;
        }
    }
    else {
        if(strcmp(msg->getName(),"ping") == 0) {
            ping_count += 1;
            ping *png = check_and_cast<ping *>(msg);
            int onu_id = png->getONU_id();
            onu_rtt[onu_id] = png->getArrivalTime().dbl();

            //EV << getFullName() << " Received ping response from ONU-" << onu_id << " and RTT = " << onu_rtt[onu_id] << endl;
            //EV << getFullName() << " onu_rtt[0] = " << onu_rtt[0] << ", onu_rtt[1] = " << onu_rtt[1] << endl;
            //EV << getFullName() << " onu_rtt[2] = " << onu_rtt[2] << ", onu_rtt[3] = " << onu_rtt[3] << endl;

            if(ping_count == onus) {
                //EV << getFullName() << " onu_total_latency[0] = " << onu_total_latency[0] << ", onu_total_latency[1] = " << onu_total_latency[1] << endl;
                cMessage *schedule_dl_gtc = new cMessage("schedule_dl_gtc");
                scheduleAt(simTime(), schedule_dl_gtc);           // when ping from all ONUs arrive, initiate the grant scheduling process

                onu_max_grant = floor((max_polling_cycle - T_guard*onus)*(ext_pon_link_datarate/onus)/8);  // in Bytes
                //EV << getFullName() <<" worst_rtt = " << worst_rtt << ", onu_max_grant = " << onu_max_grant << endl;
                for(int i = 0;i<onus;i++) {
                    onu_grant_TC3[i] = onu_max_grant;       // initializing all ONUs with maximum grant value
                    onu_buffer_TC3[i] = onu_max_grant;
                }
            }
            delete png;
        }
        else if(strcmp(msg->getName(),"schedule_dl_gtc") == 0) {        // calculating the time-instants for sending grants to onus
            scheduleAt(simTime()+(simtime_t)125e-6, msg);                          // schedule the self-message after 125 usec

            gtc_header *gtc_hdr_dl = new gtc_header("gtc_hdr_dl");
            double us_bw_map_sz = onus*8;                                  // (N x 8) Bytes
            double gtc_hdr_sz = 4 + 4 + 13 + 1 + (4*2) + us_bw_map_sz;     // total size of GTC DL header
            //EV << getFullName() << " total GTC DL Header size = " << gtc_hdr_sz << endl;
            gtc_hdr_dl->setByteLength(gtc_hdr_sz);
            gtc_hdr_dl->setDownlink(true);
            gtc_hdr_dl->setExt_pon(true);
            gtc_hdr_dl->setSeqID(++seqID);
            gtc_hdr_dl->setOlt_onu_rttArraySize(onus);
            for (int i = 0; i < onus; ++i) {
                gtc_hdr_dl->setOlt_onu_rtt(i, onu_rtt[i]);
            }

            gtc_hdr_dl->setOnu_start_time_TC2ArraySize(onus);
            gtc_hdr_dl->setOnu_grant_TC2ArraySize(onus);
            gtc_hdr_dl->setOnu_start_time_TC3ArraySize(onus);
            gtc_hdr_dl->setOnu_grant_TC3ArraySize(onus);

            double worst_rtt = *std::max_element(onu_rtt.begin(), onu_rtt.end());
            double tx_start = 0;

            for(int i = 0;i<onus;i++) {
                double onu_max_grant_TC2 = (onu_buffer_TC2[i]/(onu_buffer_TC2[i]+onu_buffer_TC3[i]))*onu_max_grant;
                double onu_max_grant_TC3 = (1-(onu_buffer_TC2[i]/(onu_buffer_TC2[i]+onu_buffer_TC3[i])))*onu_max_grant;

                onu_grant_TC2[i] = std::min(onu_buffer_TC2[i],onu_max_grant_TC2);      // granting BW using limited service policy
                onu_grant_TC3[i] = std::min(onu_buffer_TC3[i],onu_max_grant_TC3);
                //onu_grant_TC2[i] = onu_max_grant/2;                             // granting BW using fixed service policy
                //onu_grant_TC3[i] = onu_max_grant/2;

                // filling into the header packet for T-CONT 2
                onu_start_time_TC2[i] = tx_start + T_guard;
                gtc_hdr_dl->setOnu_start_time_TC2(i, onu_start_time_TC2[i]);
                gtc_hdr_dl->setOnu_grant_TC2(i, onu_grant_TC2[i]);
                // filling into the header packet for T-CONT 3
                onu_start_time_TC3[i] = tx_start + T_guard + (onu_grant_TC2[i]*8/ext_pon_link_datarate);
                gtc_hdr_dl->setOnu_start_time_TC3(i, onu_start_time_TC3[i]);
                gtc_hdr_dl->setOnu_grant_TC3(i, onu_grant_TC3[i]);
                // shifting the tx_start cursor
                tx_start += T_guard + (onu_grant_TC2[i]*8/ext_pon_link_datarate) + (onu_grant_TC3[i]*8/ext_pon_link_datarate);

                EV << getFullName() << " onu_start_time_TC2[" << i << "] = " << simTime().dbl()+2*125e-6+onu_start_time_TC2[i]-(worst_rtt/2) << " for seqID = " << seqID << endl;
                EV << getFullName() << " onu_start_time_TC3[" << i << "] = " << simTime().dbl()+2*125e-6+onu_start_time_TC3[i]-(worst_rtt/2) << " for seqID = " << seqID << endl;
            }
            EV << getFullName() << " last ONU tx finish time = " << simTime().dbl()+2*125e-6+onu_start_time_TC3[onus-1]-(worst_rtt/2)+(onu_grant_TC3[onus-1]*8/ext_pon_link_datarate) << " for seqID = " << seqID << endl;

            send(gtc_hdr_dl,"SpltGate_o");          // sending the downlink GTC header to ONUs

            cMessage *send_dl_payload = new cMessage("send_dl_payload");    // send downlink data
            scheduleAt(simTime(), send_dl_payload);

        }
        else if(strcmp(msg->getName(),"send_dl_payload") == 0) {        // sending the downlink GTC header to ONUs
            delete msg;         // not doing anything now, just keeping the provision for future
        }
    }
}


