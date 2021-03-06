//
//  main.cpp
//  Voxel Server
//
//  Created by Stephen Birarda on 03/06/13.
//  Copyright (c) 2012 High Fidelity, Inc. All rights reserved.
//

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <cstdio>

#include <OctalCode.h>
#include <NodeList.h>
#include <NodeTypes.h>
#include <EnvironmentData.h>
#include <VoxelTree.h>
#include "VoxelNodeData.h"
#include <SharedUtil.h>
#include <PacketHeaders.h>
#include <SceneUtils.h>
#include <PerfStat.h>
#include <JurisdictionSender.h>

#include "NodeWatcher.h"
#include "VoxelPersistThread.h"
#include "VoxelSendThread.h"
#include "VoxelServerPacketProcessor.h"

#ifdef _WIN32
#include "Syssocket.h"
#include "Systime.h"
#else
#include <sys/time.h>
#include <arpa/inet.h>
#include <ifaddrs.h>
#endif

#include "VoxelServer.h"

const char* LOCAL_VOXELS_PERSIST_FILE = "resources/voxels.svo";
const char* VOXELS_PERSIST_FILE = "/etc/highfidelity/voxel-server/resources/voxels.svo";
char voxelPersistFilename[MAX_FILENAME_LENGTH];
int PACKETS_PER_CLIENT_PER_INTERVAL = 10;
VoxelTree serverTree(true); // this IS a reaveraging tree 
bool wantVoxelPersist = true;
bool wantLocalDomain = false;
bool debugVoxelSending = false;
bool shouldShowAnimationDebug = false;
bool displayVoxelStats = false;
bool debugVoxelReceiving = false;
bool sendEnvironments = true;
bool sendMinimalEnvironment = false;
bool dumpVoxelsOnMove = false;
EnvironmentData environmentData[3];
int receivedPacketCount = 0;
JurisdictionMap* jurisdiction = NULL;
JurisdictionSender* jurisdictionSender = NULL;
VoxelServerPacketProcessor* voxelServerPacketProcessor = NULL;
VoxelPersistThread* voxelPersistThread = NULL;
pthread_mutex_t treeLock;
NodeWatcher nodeWatcher; // used to cleanup AGENT data when agents are killed

void attachVoxelNodeDataToNode(Node* newNode) {
    if (newNode->getLinkedData() == NULL) {
        newNode->setLinkedData(new VoxelNodeData(newNode));
    }
}

int main(int argc, const char * argv[]) {
    pthread_mutex_init(&::treeLock, NULL);
    
    qInstallMessageHandler(sharedMessageHandler);
    
    int listenPort = VOXEL_LISTEN_PORT;
    // Check to see if the user passed in a command line option for setting listen port
    const char* PORT_PARAMETER = "--port";
    const char* portParameter = getCmdOption(argc, argv, PORT_PARAMETER);
    if (portParameter) {
        listenPort = atoi(portParameter);
        if (listenPort < 1) {
            listenPort = VOXEL_LISTEN_PORT;
        }
        printf("portParameter=%s listenPort=%d\n", portParameter, listenPort);
    }

    const char* JURISDICTION_FILE = "--jurisdictionFile";
    const char* jurisdictionFile = getCmdOption(argc, argv, JURISDICTION_FILE);
    if (jurisdictionFile) {
        printf("jurisdictionFile=%s\n", jurisdictionFile);

        printf("about to readFromFile().... jurisdictionFile=%s\n", jurisdictionFile);
        jurisdiction = new JurisdictionMap(jurisdictionFile);
        printf("after readFromFile().... jurisdictionFile=%s\n", jurisdictionFile);
    } else {
        const char* JURISDICTION_ROOT = "--jurisdictionRoot";
        const char* jurisdictionRoot = getCmdOption(argc, argv, JURISDICTION_ROOT);
        if (jurisdictionRoot) {
            printf("jurisdictionRoot=%s\n", jurisdictionRoot);
        }

        const char* JURISDICTION_ENDNODES = "--jurisdictionEndNodes";
        const char* jurisdictionEndNodes = getCmdOption(argc, argv, JURISDICTION_ENDNODES);
        if (jurisdictionEndNodes) {
            printf("jurisdictionEndNodes=%s\n", jurisdictionEndNodes);
        }

        if (jurisdictionRoot || jurisdictionEndNodes) {
            ::jurisdiction = new JurisdictionMap(jurisdictionRoot, jurisdictionEndNodes);
        }
    }
    
    // should we send environments? Default is yes, but this command line suppresses sending
    const char* DUMP_VOXELS_ON_MOVE = "--dumpVoxelsOnMove";
    ::dumpVoxelsOnMove = cmdOptionExists(argc, argv, DUMP_VOXELS_ON_MOVE);
    printf("dumpVoxelsOnMove=%s\n", debug::valueOf(::dumpVoxelsOnMove));
    
    // should we send environments? Default is yes, but this command line suppresses sending
    const char* DONT_SEND_ENVIRONMENTS = "--dontSendEnvironments";
    bool dontSendEnvironments = cmdOptionExists(argc, argv, DONT_SEND_ENVIRONMENTS);
    if (dontSendEnvironments) {
        printf("Sending environments suppressed...\n");
        ::sendEnvironments = false;
    } else { 
        // should we send environments? Default is yes, but this command line suppresses sending
        const char* MINIMAL_ENVIRONMENT = "--MinimalEnvironment";
        ::sendMinimalEnvironment = cmdOptionExists(argc, argv, MINIMAL_ENVIRONMENT);
        printf("Using Minimal Environment=%s\n", debug::valueOf(::sendMinimalEnvironment));
    }
    printf("Sending environments=%s\n", debug::valueOf(::sendEnvironments));
    
    NodeList* nodeList = NodeList::createInstance(NODE_TYPE_VOXEL_SERVER, listenPort);
    setvbuf(stdout, NULL, _IOLBF, 0);
    
    // tell our NodeList about our desire to get notifications
    nodeList->addHook(&nodeWatcher);

    // Handle Local Domain testing with the --local command line
    const char* local = "--local";
    ::wantLocalDomain = cmdOptionExists(argc, argv,local);
    if (::wantLocalDomain) {
        printf("Local Domain MODE!\n");
        nodeList->setDomainIPToLocalhost();
    } else {
        const char* domainIP = getCmdOption(argc, argv, "--domain");
        if (domainIP) {
            NodeList::getInstance()->setDomainHostname(domainIP);
        }
    }

    nodeList->linkedDataCreateCallback = &attachVoxelNodeDataToNode;
    nodeList->startSilentNodeRemovalThread();
    
    srand((unsigned)time(0));
    
    const char* DISPLAY_VOXEL_STATS = "--displayVoxelStats";
    ::displayVoxelStats = cmdOptionExists(argc, argv, DISPLAY_VOXEL_STATS);
    printf("displayVoxelStats=%s\n", debug::valueOf(::displayVoxelStats));

    const char* DEBUG_VOXEL_SENDING = "--debugVoxelSending";
    ::debugVoxelSending = cmdOptionExists(argc, argv, DEBUG_VOXEL_SENDING);
    printf("debugVoxelSending=%s\n", debug::valueOf(::debugVoxelSending));

    const char* DEBUG_VOXEL_RECEIVING = "--debugVoxelReceiving";
    ::debugVoxelReceiving = cmdOptionExists(argc, argv, DEBUG_VOXEL_RECEIVING);
    printf("debugVoxelReceiving=%s\n", debug::valueOf(::debugVoxelReceiving));

    const char* WANT_ANIMATION_DEBUG = "--shouldShowAnimationDebug";
    ::shouldShowAnimationDebug = cmdOptionExists(argc, argv, WANT_ANIMATION_DEBUG);
    printf("shouldShowAnimationDebug=%s\n", debug::valueOf(::shouldShowAnimationDebug));

    // By default we will voxel persist, if you want to disable this, then pass in this parameter
    const char* NO_VOXEL_PERSIST = "--NoVoxelPersist";
    if (cmdOptionExists(argc, argv, NO_VOXEL_PERSIST)) {
        ::wantVoxelPersist = false;
    }
    printf("wantVoxelPersist=%s\n", debug::valueOf(::wantVoxelPersist));

    // if we want Voxel Persistence, load the local file now...
    bool persistantFileRead = false;
    if (::wantVoxelPersist) {

        // Check to see if the user passed in a command line option for setting packet send rate
        const char* VOXELS_PERSIST_FILENAME = "--voxelsPersistFilename";
        const char* voxelsPersistFilenameParameter = getCmdOption(argc, argv, VOXELS_PERSIST_FILENAME);
        if (voxelsPersistFilenameParameter) {
            strcpy(voxelPersistFilename, voxelsPersistFilenameParameter);
        } else {
            strcpy(voxelPersistFilename, ::wantLocalDomain ? LOCAL_VOXELS_PERSIST_FILE : VOXELS_PERSIST_FILE);
        }

        printf("loading voxels from file: %s...\n", voxelPersistFilename);

        persistantFileRead = ::serverTree.readFromSVOFile(::voxelPersistFilename);
        if (persistantFileRead) {
            PerformanceWarning warn(::shouldShowAnimationDebug,
                                    "persistVoxelsWhenDirty() - reaverageVoxelColors()", ::shouldShowAnimationDebug);
            
            // after done inserting all these voxels, then reaverage colors
            serverTree.reaverageVoxelColors(serverTree.rootNode);
            printf("Voxels reAveraged\n");
        }
        
        ::serverTree.clearDirtyBit(); // the tree is clean since we just loaded it
        printf("DONE loading voxels from file... fileRead=%s\n", debug::valueOf(persistantFileRead));
        unsigned long nodeCount         = ::serverTree.rootNode->getSubTreeNodeCount();
        unsigned long internalNodeCount = ::serverTree.rootNode->getSubTreeInternalNodeCount();
        unsigned long leafNodeCount     = ::serverTree.rootNode->getSubTreeLeafNodeCount();
        printf("Nodes after loading scene %lu nodes %lu internal %lu leaves\n", nodeCount, internalNodeCount, leafNodeCount);
        
        // now set up VoxelPersistThread
        ::voxelPersistThread = new VoxelPersistThread(&::serverTree, ::voxelPersistFilename);
        if (::voxelPersistThread) {
            ::voxelPersistThread->initialize(true);
        }
    }

    // Check to see if the user passed in a command line option for loading an old style local
    // Voxel File. If so, load it now. This is not the same as a voxel persist file
    const char* INPUT_FILE = "-i";
    const char* voxelsFilename = getCmdOption(argc, argv, INPUT_FILE);
    if (voxelsFilename) {
        serverTree.readFromSVOFile(voxelsFilename);
    }

    // Check to see if the user passed in a command line option for setting packet send rate
    const char* PACKETS_PER_SECOND = "--packetsPerSecond";
    const char* packetsPerSecond = getCmdOption(argc, argv, PACKETS_PER_SECOND);
    if (packetsPerSecond) {
        PACKETS_PER_CLIENT_PER_INTERVAL = atoi(packetsPerSecond)/INTERVALS_PER_SECOND;
        if (PACKETS_PER_CLIENT_PER_INTERVAL < 1) {
            PACKETS_PER_CLIENT_PER_INTERVAL = 1;
        }
        printf("packetsPerSecond=%s PACKETS_PER_CLIENT_PER_INTERVAL=%d\n", packetsPerSecond, PACKETS_PER_CLIENT_PER_INTERVAL);
    }
    
    // for now, initialize the environments with fixed values
    environmentData[1].setID(1);
    environmentData[1].setGravity(1.0f);
    environmentData[1].setAtmosphereCenter(glm::vec3(0.5, 0.5, (0.25 - 0.06125)) * (float)TREE_SCALE);
    environmentData[1].setAtmosphereInnerRadius(0.030625f * TREE_SCALE);
    environmentData[1].setAtmosphereOuterRadius(0.030625f * TREE_SCALE * 1.05f);
    environmentData[2].setID(2);
    environmentData[2].setGravity(1.0f);
    environmentData[2].setAtmosphereCenter(glm::vec3(0.5f, 0.5f, 0.5f) * (float)TREE_SCALE);
    environmentData[2].setAtmosphereInnerRadius(0.1875f * TREE_SCALE);
    environmentData[2].setAtmosphereOuterRadius(0.1875f * TREE_SCALE * 1.05f);
    environmentData[2].setScatteringWavelengths(glm::vec3(0.475f, 0.570f, 0.650f)); // swaps red and blue

    sockaddr senderAddress;
    
    unsigned char *packetData = new unsigned char[MAX_PACKET_SIZE];
    ssize_t packetLength;
    
    timeval lastDomainServerCheckIn = {};

    // set up our jurisdiction broadcaster...
    ::jurisdictionSender = new JurisdictionSender(::jurisdiction);
    if (::jurisdictionSender) {
        ::jurisdictionSender->initialize(true);
    }
    
    // set up our VoxelServerPacketProcessor
    ::voxelServerPacketProcessor = new VoxelServerPacketProcessor();
    if (::voxelServerPacketProcessor) {
        ::voxelServerPacketProcessor->initialize(true);
    }

    // loop to send to nodes requesting data
    while (true) {

        // send a check in packet to the domain server if DOMAIN_SERVER_CHECK_IN_USECS has elapsed
        if (usecTimestampNow() - usecTimestamp(&lastDomainServerCheckIn) >= DOMAIN_SERVER_CHECK_IN_USECS) {
            gettimeofday(&lastDomainServerCheckIn, NULL);
            NodeList::getInstance()->sendDomainServerCheckIn();
        }
        
        if (nodeList->getNodeSocket()->receive(&senderAddress, packetData, &packetLength) &&
            packetVersionMatch(packetData)) {

            int numBytesPacketHeader = numBytesForPacketHeader(packetData);

            if (packetData[0] == PACKET_TYPE_HEAD_DATA) {
                // If we got a PACKET_TYPE_HEAD_DATA, then we're talking to an NODE_TYPE_AVATAR, and we
                // need to make sure we have it in our nodeList.
                uint16_t nodeID = 0;
                unpackNodeId(packetData + numBytesPacketHeader, &nodeID);
                Node* node = NodeList::getInstance()->addOrUpdateNode(&senderAddress,
                                                       &senderAddress,
                                                       NODE_TYPE_AGENT,
                                                       nodeID);

                NodeList::getInstance()->updateNodeWithData(node, packetData, packetLength);
            } else if (packetData[0] == PACKET_TYPE_PING) {
                // If the packet is a ping, let processNodeData handle it.
                NodeList::getInstance()->processNodeData(&senderAddress, packetData, packetLength);
            } else if (packetData[0] == PACKET_TYPE_DOMAIN) {
                NodeList::getInstance()->processNodeData(&senderAddress, packetData, packetLength);
            } else if (packetData[0] == PACKET_TYPE_VOXEL_JURISDICTION_REQUEST) {
                if (::jurisdictionSender) {
                    ::jurisdictionSender->queueReceivedPacket(senderAddress, packetData, packetLength);
                }
            } else if (::voxelServerPacketProcessor) {
                ::voxelServerPacketProcessor->queueReceivedPacket(senderAddress, packetData, packetLength);
            } else {
                printf("unknown packet ignored... packetData[0]=%c\n", packetData[0]);
            }
        }
    }
    
    if (::jurisdiction) {
        delete ::jurisdiction;
    }
    
    if (::jurisdictionSender) {
        ::jurisdictionSender->terminate();
        delete ::jurisdictionSender;
    }

    if (::voxelServerPacketProcessor) {
        ::voxelServerPacketProcessor->terminate();
        delete ::voxelServerPacketProcessor;
    }

    if (::voxelPersistThread) {
        ::voxelPersistThread->terminate();
        delete ::voxelPersistThread;
    }
    
    // tell our NodeList we're done with notifications
    nodeList->removeHook(&nodeWatcher);
    
    pthread_mutex_destroy(&::treeLock);

    return 0;
}


