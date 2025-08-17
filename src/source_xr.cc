/*
 * source.cc
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

/*
 * Each source generates packets following some probability distribution.
 * The average inter-arrival time is derived using the maximum datarate of the
 * SourceApp and average packet size.
 * The generateEvent is a self-message which is scheduled back-to-back to create
 * a new packet. Immediately the packet is transmitted to the corresponding ONU.
 */

class XR_Device : public cSimpleModule
{
    private:
        cQueue source_queue;                        // Queue for holding packets to be sent to ONUs/SFUs
        double avgFrameSize;
        double avgDataRate;
        double ArrivalRate;
        double pkt_interval;                     // inter-packet generation interval
        double pkt_size;
        double wireless_datarate;
        double wap_dist;
        cMessage *generateEvent = nullptr;
        cMessage *sendEvent = nullptr; // new event to know when transmission finishes

    public:
        virtual ~XR_Device();

    protected:
        // The following redefined virtual function holds the algorithm.
        virtual void initialize() override;
        virtual void handleMessage(cMessage *msg) override;
        virtual ethPacket *generateNewPacket();
};

// The module class needs to be registered with OMNeT++
Define_Module(XR_Device);

XR_Device::~XR_Device()
{
    cancelAndDelete(generateEvent);
    cancelAndDelete(sendEvent);

    // Clean up queues
    while (!source_queue.isEmpty()) {
        delete source_queue.pop();
    }
}

void XR_Device::initialize()
{
    //arrivalSignal = registerSignal("generation");               // registering the signal

    wireless_datarate = par("throughput").doubleValue();
    wap_dist = par("wap_distance").doubleValue();

    source_queue.setName("source_queue");

    avgDataRate = par("dataRate").doubleValue();                              // get the load factor from NED file
    ArrivalRate = par("frameRate").doubleValue();                             // get the max ONU datarate from NED file

    // Initialize variables
    double mean = 1.0/ArrivalRate;
    double std = 2e-3;                                          // std = 2 msec
    pkt_interval = truncnormal(mean, std);                       // packet inter-arrival times are generated following truncnormal distribution

    generateEvent = new cMessage("generateEvent");              // self-message is generated for next packet generation
    //emit(arrivalSignal,pkt_interval);
    sendEvent = new cMessage("sendEvent");          // initializing here
    // schedule first packet generation
    scheduleAt(simTime(), generateEvent);
}

void XR_Device::handleMessage(cMessage *msg)
{
    if (strcmp(msg->getName(), "generateEvent") == 0) {
        // Schedule next frame generation
        double mean = 1.0 / ArrivalRate;
        double std  = 2e-3;
        pkt_interval = truncnormal(mean, std);
        scheduleAt(simTime() + pkt_interval, generateEvent);
        EV << getFullName() << " Next XR frame scheduled at " << simTime() + pkt_interval << endl;

        // Generate a frame
        avgFrameSize = avgDataRate/(8*ArrivalRate);                        // framesize = datarate (bps)/(8*fps)
        double frameSize = truncnormal(avgFrameSize, 0.105*avgFrameSize);
        //double frameSize = 0.5*avgFrameSize;
        int num_pkts = ceil(frameSize / 1500.0);

        for (int i = 0; i < num_pkts; i++) {
            int payload = (i == num_pkts - 1) ? (frameSize - (num_pkts - 1) * 1500) : 1500;
            pkt_size = payload + 42;  // Ethernet overhead
            ethPacket *pkt = generateNewPacket();
            source_queue.insert(pkt);
        }

        // send packet if the channel is free
        if (!sendEvent->isScheduled()) {        // if no transmission is happening
            scheduleAt(simTime(), sendEvent);   // schedule send event immediately
        }
    }
    else if(strcmp(msg->getName(),"sendEvent") == 0) {
        if(!source_queue.isEmpty()) {
            // dequeue next packet
            ethPacket *pkt = check_and_cast<ethPacket *>(source_queue.pop());

            cModule *targetModule = getParentModule()->getSubmodule("waps", (getIndex()*2));
            // compute delays
            simtime_t propDelay = wap_dist / (3e8);
            simtime_t txDuration = pkt->getBitLength() / wireless_datarate;
            // send it
            sendDirect(pkt, propDelay, txDuration, targetModule->gate("Src_in"));
            scheduleAt(simTime()+txDuration, sendEvent);
            EV << getFullName() << " Sent XR packet at = " << simTime();
            EV << " and next packet will be sent at = " << simTime()+txDuration << endl;
        }
        else {
            EV << getFullName() << " Queue is empty now!" << endl;
            // no need to re-schedule sendEvent in this case.
        }
    }
}

ethPacket *XR_Device::generateNewPacket()
{
    ethPacket *pkt = new ethPacket("xr_data");
    pkt->setByteLength(pkt_size);                              // generating packets of same size
    pkt->setGenerationTime(simTime());
    //EV << "[srcXR" << getIndex() << "] New packet generated with size (bytes): " << pkt_size << endl;
    return pkt;
}


