/*
 * source.cc (modified for sequential transmission)
 *
 *  Created on: 30 July 2025
 *      Author: mondals
 */

#include <string.h>
#include <math.h>
#include <omnetpp.h>
#include <numeric>
#include <algorithm>

#include "sim_params.h"
#include "ethPacket_m.h"
#include "ping_m.h"
#include "gtc_header_m.h"

using namespace std;
using namespace omnetpp;

class Background_Device : public cSimpleModule
{
  private:
    cQueue source_queue;
    double Load;
    double ArrivalRate;
    double pkt_interval;
    double wireless_datarate;
    double wap_dist;
    cMessage *generateEvent = nullptr;
    cMessage *sendEvent = nullptr;

  public:
    virtual ~Background_Device();

  protected:
    virtual void initialize() override;
    virtual void handleMessage(cMessage *msg) override;
    virtual ethPacket *generateNewPacket();
};

Define_Module(Background_Device);

Background_Device::~Background_Device()
{
    cancelAndDelete(generateEvent);
    cancelAndDelete(sendEvent);

    // Clean up queues
    while (!source_queue.isEmpty()) {
        delete source_queue.pop();
    }
}

void Background_Device::initialize()
{
    wireless_datarate = par("throughput").doubleValue();
    wap_dist = par("wap_distance").doubleValue();

    source_queue.setName("source_queue");

    Load = par("load").doubleValue();
    double R_o = par("dataRate").doubleValue();
    ArrivalRate = Load*R_o/(8*pkt_sz_avg);
    EV << getFullName() << " wireless_datarate = " << wireless_datarate << ", wap_dist = " << wap_dist;
    EV << ", Load = " << Load << ", ArrivalRate = " << ArrivalRate << endl;

    generateEvent = new cMessage("generateEvent");  // initializing here
    sendEvent = new cMessage("sendEvent");          // initializing here
    // schedule first packet generation
    scheduleAt(simTime(), generateEvent);           // scheduling packet generation for the first time
}

void Background_Device::handleMessage(cMessage *msg)
{
    if(strcmp(msg->getName(),"generateEvent") == 0) {
        // generate a packet and put into queue
        ethPacket *pkt = generateNewPacket();
        source_queue.insert(pkt);

        // schedule next packet generation
        pkt_interval = exponential(1/ArrivalRate);
        scheduleAt(simTime() + pkt_interval, generateEvent);
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

            cModule *targetModule = getParentModule()->getSubmodule("waps", getIndex());
            // compute delays
            simtime_t propDelay = wap_dist / (3e8);
            simtime_t txDuration = pkt->getBitLength() / wireless_datarate;
            // send it
            sendDirect(pkt, propDelay, txDuration, targetModule->gate("Src_in"));
            scheduleAt(simTime()+txDuration, sendEvent);
            EV << getFullName() << " Sent background packet at = " << simTime();
            EV << " and next packet will be sent at = " << simTime()+txDuration << endl;
        }
        else {
            EV << getFullName() << " Queue is empty now!" << endl;
            // no need to re-schedule sendEvent in this case.
        }
    }
}

ethPacket *Background_Device::generateNewPacket()
{
    int pkt_size = intuniform(64,1542);
    ethPacket *pkt = new ethPacket("bkg_data");
    pkt->setByteLength(pkt_size);
    pkt->setGenerationTime(simTime());
    //EV << getFullName() << " New packet generated with size (bytes): " << avgPacketSize << endl;
    return pkt;
}
