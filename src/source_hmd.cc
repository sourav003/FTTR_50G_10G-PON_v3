/*
 * source_hmd.cc
 *
 *  Created on: 8 Aug 2025
 *      Author: mondals
 */

#include <string.h>
#include <math.h>
#include <omnetpp.h>
#include <numeric>   // Required for std::iota
#include <algorithm> // Required for std::sort
#include <fstream>   // <-- add this
#include <iostream>  // (optional, for debugging with std::cout)

#include "sim_params.h"
#include "ethPacket_m.h"
#include "ping_m.h"
#include "gtc_header_m.h"

using namespace std;
using namespace omnetpp;

/*
 * Each source generates packets following some probability distribution.
 * The average inter-arrival time is derived using the maximum datarate of the
 * SourceApp and average packet size.
 * The generateEvent is a self-message which is scheduled back-to-back to create
 * a new packet. Immediately the packet is transmitted to the corresponding ONU.
 */

class HMD_Device : public cSimpleModule
{
    private:
        cQueue source_queue;                        // Queue for holding packets to be sent to ONUs/SFUs
        double avgPacketSize;
        double ArrivalRate;
        double pkt_interval;                     // inter-packet generation interval
        double wireless_datarate;
        double wap_dist;
        vector<double> dist_values;

        cMessage *generateEvent = nullptr;
        cMessage *sendEvent = nullptr; // new event to know when transmission finishes

        //simsignal_t arrivalSignal;               // to send signals for statistics collection

    public:
        virtual ~HMD_Device();

    protected:
        // The following redefined virtual function holds the algorithm.
        virtual void initialize() override;
        virtual void handleMessage(cMessage *msg) override;
        virtual ethPacket *generateNewPacket();
};

// The module class needs to be registered with OMNeT++
Define_Module(HMD_Device);

HMD_Device::~HMD_Device()
{
    cancelAndDelete(generateEvent);
    cancelAndDelete(sendEvent);

    // Clean up queues
    while (!source_queue.isEmpty()) {
        delete source_queue.pop();
    }
}

void HMD_Device::initialize()
{
    wireless_datarate = par("throughput").doubleValue();

    if(dist_values.empty()) {
        std::ifstream file("ap_hmd.csv");
        double value;
        while(file >> value) {
            dist_values.push_back(value);
        }
    }
    int idx = getIndex();
    wap_dist = (idx < (int)dist_values.size()) ? dist_values[idx] : par("wap_distance").doubleValue();
    //par("wap_distance").setDoubleValue(wap_dist);
    //file.close();
    EV << getFullName() << " wap_distance = " << wap_dist << endl;

    source_queue.setName("source_queue");

    avgPacketSize = par("meanPacketSize").doubleValue();                      // get the avg packet size from NED file
    ArrivalRate   = par("sampleRate").doubleValue();                          // get the HMD location sample rate from NED file (1/15e-3 per sec)

    // Initialize variables
    double sd = 0.5;
    double scale_b = sd*sqrt(ArrivalRate);                      // beta = sd^2/mean, assuming sd = 1 ms
    double shape_a = (1/ArrivalRate)/scale_b;                   // alpha = mean/beta
    pkt_interval = 1e-3*gamma_d(shape_a,scale_b);               // packet inter-arrival times are generated following gamma distribution

    generateEvent = new cMessage("generateEvent");              // self-message is generated for next packet generation
    //emit(arrivalSignal,pkt_interval);
    sendEvent = new cMessage("sendEvent");                      // initializing here
    // schedule first packet generation
    scheduleAt(simTime(), generateEvent);
}

void HMD_Device::handleMessage(cMessage *msg)
{
    if(strcmp(msg->getName(),"generateEvent") == 0) {
        // generate a packet and put into queue
        ethPacket *pkt = generateNewPacket();
        source_queue.insert(pkt);

        double sd = 0.5;
        double scale_b = sd*sqrt(ArrivalRate);                      // beta = sd^2/mean, assuming sd = 1 ms
        double shape_a = (1/ArrivalRate)/scale_b;                   // alpha = mean/beta
        pkt_interval = 1e-3*gamma_d(shape_a,scale_b);               // packet inter-arrival times are generated following gamma distribution

        scheduleAt(simTime()+pkt_interval, generateEvent);      // scheduling the next packet generation
        EV << getFullName() << " Next packet generation is scheduled at = " << simTime()+pkt_interval << endl;

        // send packet if the channel is free
        if (!sendEvent->isScheduled()) {        // if no transmission is happening
            scheduleAt(simTime(), sendEvent);   // schedule send event immediately
        }
    }
    else if(strcmp(msg->getName(),"sendEvent") == 0) {
        if(!source_queue.isEmpty()) {
            // dequeue next packet
            ethPacket *pkt = check_and_cast<ethPacket *>(source_queue.pop());

            cModule *targetModule = getParentModule()->getSubmodule("waps", (getIndex()*2+1));
            // compute delays
            simtime_t propDelay = wap_dist / (3e8);
            simtime_t txDuration = pkt->getBitLength() / wireless_datarate;
            // send it
            sendDirect(pkt, propDelay, txDuration, targetModule->gate("Src_in"));
            scheduleAt(simTime()+txDuration, sendEvent);
            EV << getFullName() << " Sent HMD packet at = " << simTime();
            EV << " and next packet will be sent at = " << simTime()+txDuration << endl;
        }
        else {
            EV << getFullName() << " Queue is empty now!" << endl;
            // no need to re-schedule sendEvent in this case.
        }
    }
}

ethPacket *HMD_Device::generateNewPacket()
{
    ethPacket *pkt = new ethPacket("hmd_data");
    pkt->setByteLength(avgPacketSize);                              // generating packets of same size
    pkt->setGenerationTime(simTime());
    //EV << getFullName() << " New packet generated with size (bytes): " << avgPacketSize << endl;
    return pkt;
}






