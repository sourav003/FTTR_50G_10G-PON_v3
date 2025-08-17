/*
 * source_hpt.cc
 *
 *  Created on: 8 Aug 2025
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

class Haptic_Device : public cSimpleModule
{
    private:
        cQueue source_queue;                        // Queue for holding packets to be sent to ONUs/SFUs
        double avgPacketSize;
        double ArrivalRate;
        double pkt_interval;                     // inter-packet generation interval
        double wireless_datarate;
        double wap_dist;
        cMessage *generateEvent = nullptr;
        cMessage *sendEvent = nullptr;

    public:
        virtual ~Haptic_Device();

    protected:
        virtual void initialize() override;
        virtual void handleMessage(cMessage *msg) override;
        virtual ethPacket *generateNewPacket();
};

// The module class needs to be registered with OMNeT++
Define_Module(Haptic_Device);

Haptic_Device::~Haptic_Device()
{
    cancelAndDelete(generateEvent);
    cancelAndDelete(sendEvent);

    // Clean up queues
    while (!source_queue.isEmpty()) {
        delete source_queue.pop();
    }
}

void Haptic_Device::initialize()
{
    wireless_datarate = par("throughput").doubleValue();
    wap_dist = par("wap_distance").doubleValue();

    source_queue.setName("source_queue");

    avgPacketSize = par("meanPacketSize").doubleValue();                      // get the avg packet size from NED file
    ArrivalRate   = par("sampleRate").doubleValue();                          // get the HMD location sample rate from NED file (1/11e-3 per sec)

    // Initialize variables
    double mean = 1e-3*(1.0/ArrivalRate);                       // mean = 10 ms
    double std = 4e-3;                                          // sd = 4 ms
    // calculating generalized Pareto distribution parameters
    double a = 1 + std::sqrt(1 + (mean*mean)/(pow(std, 2)));
    double b = mean * (a - 1) / a;
    double c = 0.0;

    pkt_interval = pareto_shifted(a, b, c);                      // packet inter-arrival times are generated following GP distribution

    generateEvent = new cMessage("generateEvent");              // self-message is generated for next packet generation
    //emit(arrivalSignal,pkt_interval);
    sendEvent = new cMessage("sendEvent");          // initializing here

    // schedule first packet generation
    scheduleAt(simTime(), generateEvent);
}

void Haptic_Device::handleMessage(cMessage *msg)
{
    if(strcmp(msg->getName(),"generateEvent") == 0) {
        // generate a packet and put into queue
        ethPacket *pkt = generateNewPacket();
        source_queue.insert(pkt);

        // schedule next packet generation
        double mean = 1e-3*(1.0/ArrivalRate);                       // mean = 10 ms
        double std = 4e-3;                                          // sd = 4 ms
        // calculating generalized Pareto distribution parameters
        double a = 1 + std::sqrt(1 + (mean*mean)/(pow(std, 2)));
        double b = mean * (a - 1) / a;
        double c = 0.0;

        pkt_interval = pareto_shifted(a, b, c);                      // packet inter-arrival times are generated following GP distribution
        scheduleAt(simTime() + pkt_interval, generateEvent);      // scheduling the next packet generation
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

            cModule *targetModule = getParentModule()->getSubmodule("waps", (getIndex()*2));
            // compute delays
            simtime_t propDelay = wap_dist / (3e8);
            simtime_t txDuration = pkt->getBitLength() / wireless_datarate;
            // send it
            sendDirect(pkt, propDelay, txDuration, targetModule->gate("Src_in"));
            scheduleAt(simTime()+txDuration, sendEvent);
            EV << getFullName() << " Sent Haptic packet at = " << simTime();
            EV << " and next packet will be sent at = " << simTime()+txDuration << endl;
        }
        else {
            EV << getFullName() << " Queue is empty now!" << endl;
            // no need to re-schedule sendEvent in this case.
        }
    }
}

ethPacket *Haptic_Device::generateNewPacket()
{
    ethPacket *pkt = new ethPacket("haptic_data");
    pkt->setByteLength(avgPacketSize);                              // generating packets of same size
    pkt->setGenerationTime(simTime());
    //EV << getFullName() << " New packet generated with size (bytes): " << avgPacketSize << endl;
    return pkt;
}


