//
//  main.cpp
//  assignment-client
//
//  Created by Stephen Birarda on 8/22/13.
//  Copyright (c) 2013 High Fidelity, Inc. All rights reserved.
//

#include <arpa/inet.h>
#include <errno.h>
#include <signal.h>
#include <sys/time.h>
#include <sys/wait.h>

#include <QtCore/QCoreApplication>

#include "Agent.h"
#include <Assignment.h>
#include <AudioMixer.h>
#include <AvatarMixer.h>
#include <Logging.h>
#include <NodeList.h>
#include <PacketHeaders.h>
#include <SharedUtil.h>

const long long ASSIGNMENT_REQUEST_INTERVAL_USECS = 1 * 1000 * 1000;
const char PARENT_TARGET_NAME[] = "assignment-client-monitor";
const char CHILD_TARGET_NAME[] = "assignment-client";

pid_t* childForks = NULL;
sockaddr_in customAssignmentSocket = {};
int numForks = 0;

void childClient() {
    // this is one of the child forks or there is a single assignment client, continue assignment-client execution
    
    // set the logging target to the the CHILD_TARGET_NAME
    Logging::setTargetName(CHILD_TARGET_NAME);
    
    // create a NodeList as an unassigned client
    NodeList* nodeList = NodeList::createInstance(NODE_TYPE_UNASSIGNED);
    
    // set the custom assignment socket if we have it
    if (customAssignmentSocket.sin_addr.s_addr != 0) {
        nodeList->setAssignmentServerSocket((sockaddr*) &customAssignmentSocket);
    }
    
    // change the timeout on the nodelist socket to be as often as we want to re-request
    nodeList->getNodeSocket()->setBlockingReceiveTimeoutInUsecs(ASSIGNMENT_REQUEST_INTERVAL_USECS);
    
    timeval lastRequest = {};
    
    unsigned char packetData[MAX_PACKET_SIZE];
    ssize_t receivedBytes = 0;
    
    sockaddr_in senderSocket = {};
    
    // create a request assignment, accept all assignments, pass the desired pool (if it exists)
    Assignment requestAssignment(Assignment::RequestCommand, Assignment::AllTypes);
    
    while (true) {
        if (usecTimestampNow() - usecTimestamp(&lastRequest) >= ASSIGNMENT_REQUEST_INTERVAL_USECS) {
            gettimeofday(&lastRequest, NULL);
            // if we're here we have no assignment, so send a request
            qDebug() << "Sending an assignment request -" << requestAssignment << "\n";
            nodeList->sendAssignment(requestAssignment);
        }
        
        if (nodeList->getNodeSocket()->receive((sockaddr*) &senderSocket, packetData, &receivedBytes) &&
            (packetData[0] == PACKET_TYPE_DEPLOY_ASSIGNMENT || packetData[0] == PACKET_TYPE_CREATE_ASSIGNMENT)
            && packetVersionMatch(packetData)) {
            
            // construct the deployed assignment from the packet data
            Assignment deployedAssignment(packetData, receivedBytes);
            
            qDebug() << "Received an assignment -" << deployedAssignment << "\n";
            
            // switch our nodelist DOMAIN_IP
            if (packetData[0] == PACKET_TYPE_CREATE_ASSIGNMENT ||
                deployedAssignment.getAttachedPublicSocket()->sa_family == AF_INET) {
                
                in_addr domainSocketAddr = {};
                
                if (packetData[0] == PACKET_TYPE_CREATE_ASSIGNMENT) {
                    // the domain server IP address is the address we got this packet from
                    domainSocketAddr = senderSocket.sin_addr;
                } else {
                    // grab the domain server IP address from the packet from the AS
                    domainSocketAddr = ((sockaddr_in*) deployedAssignment.getAttachedPublicSocket())->sin_addr;
                }
                
                nodeList->setDomainIP(inet_ntoa(domainSocketAddr));
                
                qDebug("Destination IP for assignment is %s\n", inet_ntoa(domainSocketAddr));
                
                if (deployedAssignment.getType() == Assignment::AudioMixerType) {
                    AudioMixer::run();
                } else if (deployedAssignment.getType() == Assignment::AvatarMixerType) {
                    AvatarMixer::run();
                } else {
                    // figure out the URL for the script for this agent assignment
                    QString scriptURLString("http://%1:8080/assignment/%2");
                    scriptURLString = scriptURLString.arg(inet_ntoa(domainSocketAddr),
                                                          deployedAssignment.getUUIDStringWithoutCurlyBraces());
                    
                    qDebug() << "Starting an Agent assignment-client with script at" << scriptURLString << "\n";
                    
                    Agent scriptAgent;
                    scriptAgent.run(QUrl(scriptURLString));
                }
            } else {
                qDebug("Received a bad destination socket for assignment.\n");
            }
            
            qDebug("Assignment finished or never started - waiting for new assignment\n");
            
            // reset our NodeList by switching back to unassigned and clearing the list
            nodeList->setOwnerType(NODE_TYPE_UNASSIGNED);
            nodeList->clear();
            
            // reset the logging target to the the CHILD_TARGET_NAME
            Logging::setTargetName(CHILD_TARGET_NAME);
        }
    }
}

void sigchldHandler(int sig) {
    pid_t processID;
    int status;
    
    while ((processID = waitpid(-1, &status, WNOHANG)) != -1) {
        if (processID == 0) {
            // there are no more children to process, break out of here
            break;
        }
        
        int newForkProcessID = 0;
        
        // find the dead process in the array of child forks
        for (int i = 0; i < ::numForks; i++) {
            if (::childForks[i] == processID) {
                
                newForkProcessID = fork();
                if (newForkProcessID == 0) {
                    // this is the child, call childClient
                    childClient();
                    
                    // break out so we don't fork bomb
                    break;
                } else {
                    // this is the parent, replace the dead process with the new one
                    ::childForks[i] = newForkProcessID;
                   
                    qDebug("Replaced dead %d with new fork %d\n", processID, newForkProcessID);
                    
                    break;
                }
            }
        }
    }
}

void parentMonitor() {
    
    struct sigaction sa;
    
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sigchldHandler;
    
    sigaction(SIGCHLD, &sa, NULL);
    
    pid_t childID = 0;
    
    // don't bail until all children have finished
    while ((childID = waitpid(-1, NULL, 0))) {
        if (errno == ECHILD) {
            break;
        }
    }
    
    // delete the array of pid_t holding the forked process IDs
    delete[] ::childForks;
}

int main(int argc, const char* argv[]) {
    
    QCoreApplication app(argc, (char**) argv);
    
    setvbuf(stdout, NULL, _IOLBF, 0);
    
    // use the verbose message handler in Logging
    qInstallMessageHandler(Logging::verboseMessageHandler);
    
    // start the Logging class with the parent's target name
    Logging::setTargetName(PARENT_TARGET_NAME);
    
    const char CUSTOM_ASSIGNMENT_SERVER_HOSTNAME_OPTION[] = "-a";
    const char CUSTOM_ASSIGNMENT_SERVER_PORT_OPTION[] = "-p";
    
    // grab the overriden assignment-server hostname from argv, if it exists
    const char* customAssignmentServerHostname = getCmdOption(argc, argv, CUSTOM_ASSIGNMENT_SERVER_HOSTNAME_OPTION);
    
    if (customAssignmentServerHostname) {
        const char* customAssignmentServerPortString = getCmdOption(argc, argv, CUSTOM_ASSIGNMENT_SERVER_PORT_OPTION);
        unsigned short assignmentServerPort = customAssignmentServerPortString
            ? atoi(customAssignmentServerPortString) : ASSIGNMENT_SERVER_PORT;
        
        ::customAssignmentSocket = socketForHostnameAndHostOrderPort(customAssignmentServerHostname, assignmentServerPort);
    }
    
    const char* NUM_FORKS_PARAMETER = "-n";
    const char* numForksString = getCmdOption(argc, argv, NUM_FORKS_PARAMETER);
    
    int processID = 0;
    
    if (numForksString) {
        ::numForks = atoi(numForksString);
        qDebug("Starting %d assignment clients\n", ::numForks);
        
        ::childForks = new pid_t[::numForks];
        
        // fire off as many children as we need (this is one less than the parent since the parent will run as well)
        for (int i = 0; i < ::numForks; i++) {
            processID = fork();
            
            if (processID == 0) {
                // this is in one of the children, break so we don't start a fork bomb
                break;
            } else {
                // this is in the parent, save the ID of the forked process
                childForks[i] = processID;
            }
        }
    }
    
    if (processID == 0 || ::numForks == 0) {
        childClient();
    } else {
        parentMonitor();
    }
}