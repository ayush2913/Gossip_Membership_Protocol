/**********************************
 * FILE NAME: MP1Node.cpp
 *
 * DESCRIPTION: Membership protocol run by this Node.
 * 				Definition of MP1Node class functions.
 **********************************/

#include "MP1Node.h"

/*
 * Note: You can change/add any functions in MP1Node.{h,cpp}
 */

/**
 * Overloaded Constructor of the MP1Node class
 * You can add new members to the class if you think it
 * is necessary for your logic to work
 */
MP1Node::MP1Node(Member *member, Params *params, EmulNet *emul, Log *log, Address *address) {
	for( int i = 0; i < 6; i++ ) {
		NULLADDR[i] = 0;
	}
	this->memberNode = member;
	this->emulNet = emul;
	this->log = log;
	this->par = params;
	this->memberNode->addr = *address;
}

/**
 * Destructor of the MP1Node class
 */
MP1Node::~MP1Node() {}

/**
 * FUNCTION NAME: recvLoop
 *
 * DESCRIPTION: This function receives message from the network and pushes into the queue
 * 				This function is called by a node to receive messages currently waiting for it
 */
int MP1Node::recvLoop() {
    if ( memberNode->bFailed ) {
    	return false;
    }
    else {
    	return emulNet->ENrecv(&(memberNode->addr), enqueueWrapper, NULL, 1, &(memberNode->mp1q));
    }
}

/**
 * FUNCTION NAME: enqueueWrapper
 *
 * DESCRIPTION: Enqueue the message from Emulnet into the queue
 */
int MP1Node::enqueueWrapper(void *env, char *buff, int size) {
	Queue q;
	return q.enqueue((queue<q_elt> *)env, (void *)buff, size);
}

/**
 * FUNCTION NAME: nodeStart
 *
 * DESCRIPTION: This function bootstraps the node
 * 				All initializations routines for a member.
 * 				Called by the application layer.
 */
void MP1Node::nodeStart(char *servaddrstr, short servport) {
    Address joinaddr;
    joinaddr = getJoinAddress();

    // Self booting routines
    if( initThisNode(&joinaddr) == -1 ) {
#ifdef DEBUGLOG
        log->LOG(&memberNode->addr, "init_thisnode failed. Exit.");
#endif
        exit(1);
    }

    if( !introduceSelfToGroup(&joinaddr) ) {
        finishUpThisNode();
#ifdef DEBUGLOG
        log->LOG(&memberNode->addr, "Unable to join self to group. Exiting.");
#endif
        exit(1);
    }

    return;
}

/**
 * FUNCTION NAME: initThisNode
 *
 * DESCRIPTION: Find out who I am and start up
 */
int MP1Node::initThisNode(Address *joinaddr) {
	/*
	 * This function is partially implemented and may require changes
	 */
	int id = *(int*)(&memberNode->addr.addr);
	int port = *(short*)(&memberNode->addr.addr[4]);

	memberNode->bFailed = false;
	memberNode->inited = true;
	memberNode->inGroup = false;
    // node is up!
	memberNode->nnb = 0;
	memberNode->heartbeat = 0;
	memberNode->pingCounter = TFAIL;
	memberNode->timeOutCounter = -1;
    initMemberListTable(memberNode);

    return 0;
}

/**
 * FUNCTION NAME: introduceSelfToGroup
 *
 * DESCRIPTION: Join the distributed system
 */
int MP1Node::introduceSelfToGroup(Address *joinaddr) {
	MessageHdr *msg;
#ifdef DEBUGLOG
    static char s[1024];
#endif

    if ( 0 == memcmp((char *)&(memberNode->addr.addr), (char *)&(joinaddr->addr), sizeof(memberNode->addr.addr))) {
        // I am the group booter (first process to join the group). Boot up the group
#ifdef DEBUGLOG
        log->LOG(&memberNode->addr, "Starting up group...");
#endif
        memberNode->inGroup = true;
    }
    else {
        size_t msgsize = sizeof(MessageHdr) + sizeof(joinaddr->addr) + sizeof(long) + 1;
        msg = (MessageHdr *) malloc(msgsize * sizeof(char));

        // create JOINREQ message: format of data is {struct Address myaddr}
        msg->msgType = JOINREQ;
        memcpy((char *)(msg+1), &memberNode->addr.addr, sizeof(memberNode->addr.addr));
        memcpy((char *)(msg+1) + 1 + sizeof(memberNode->addr.addr), &memberNode->heartbeat, sizeof(long));

#ifdef DEBUGLOG
        sprintf(s, "Trying to join...");
        log->LOG(&memberNode->addr, s);
#endif

        // send JOINREQ message to introducer member
        emulNet->ENsend(&memberNode->addr, joinaddr, (char *)msg, msgsize);

        free(msg);
    }

    return 1;

}

/**
 * FUNCTION NAME: finishUpThisNode
 *
 * DESCRIPTION: Wind up this node and clean up state
 */
int MP1Node::finishUpThisNode(){
   /*
    * Your code goes here
    */

    memberNode->inited = false;
    memberNode->inGroup = false;
    memberNode->nnb = 0;
    memberNode->heartbeat = 0;
    memberNode->pingCounter=TFAIL;
    initMemberListTable(memberNode);
    return 0;

}

/**
 * FUNCTION NAME: nodeLoop
 *
 * DESCRIPTION: Executed periodically at each member
 * 				Check your messages in queue and perform membership protocol duties
 */
void MP1Node::nodeLoop() {
    if (memberNode->bFailed) {
    	return;
    }

    // Check my messages
    checkMessages();

    // Wait until you're in the group...
    if( !memberNode->inGroup ) {
    	return;
    }

    // ...then jump in and share your responsibilites!
    nodeLoopOps();

    return;
}

/**
 * FUNCTION NAME: checkMessages
 *
 * DESCRIPTION: Check messages in the queue and call the respective message handler
 */
void MP1Node::checkMessages() {
    void *ptr;
    int size;

    // Pop waiting messages from memberNode's mp1q
    while ( !memberNode->mp1q.empty() ) {
    	ptr = memberNode->mp1q.front().elt;
    	size = memberNode->mp1q.front().size;
    	memberNode->mp1q.pop();
    	recvCallBack((void *)memberNode, (char *)ptr, size);
    }
    return;
}

/**
 * FUNCTION NAME: recvCallBack
 *
 * DESCRIPTION: Message handler for different message types
 */
bool MP1Node::recvCallBack(void *env, char *data, int size ) {
	/*
	 * Your code goes here
	 */

    MessageHdr* msg = (MessageHdr*) malloc(size*sizeof(char));
    memcpy(msg, data, sizeof(MessageHdr));
    int id;
    short port;
    long heartbeat;
    if(msg->msgType == JOINREQ){

        memcpy(&id, data + sizeof(MessageHdr), sizeof(int));
        memcpy(&port, data + sizeof(MessageHdr) + sizeof(int), sizeof(short));
        memcpy(&heartbeat, data + sizeof(MessageHdr) + sizeof(int) + sizeof(short), sizeof(long));
        Address *newNodeAddress = addMember(id,port,heartbeat, memberNode->timeOutCounter);
        sendMessage(JOINREP, newNodeAddress);
    }
    else if(msg->msgType == JOINREP){

        memberNode->inGroup = true;

    }else if(msg->msgType == HEARTBEAT){
        memcpy(&id, data + sizeof(MessageHdr), sizeof(int));
        memcpy(&port, data + sizeof(MessageHdr) + sizeof(int), sizeof(short));
        memcpy(&heartbeat, data + sizeof(MessageHdr) + sizeof(int) + sizeof(short), sizeof(long));
        addMember(id,port,heartbeat, memberNode->timeOutCounter);
    }

    return true;

}

Address* MP1Node::addMember(int id, short port, long heartbeat, long timestamp){
    Address *srcAddr = (Address *)malloc(sizeof(Address));
    *(int *)(&srcAddr->addr[0]) = id;
    *(short *)(&srcAddr->addr[4]) = port;
    if (!updateMember(id, port, heartbeat)) {
        MemberListEntry *newMember = new MemberListEntry(id, port, heartbeat, timestamp);
        memberNode->memberList.insert(memberNode->memberList.begin(), *newMember);
        
        #ifdef DEBUGLOG
        log->logNodeAdd(&(memberNode->addr), srcAddr);
        #endif
    }

    return srcAddr;
}

bool MP1Node::updateMember(int id, short port, long heartbeat){
    for (vector<MemberListEntry>::iterator it = memberNode->memberList.begin(); it != memberNode->memberList.end(); ++it) {
        if(it->id == id && it->port == port){
            it->heartbeat = heartbeat;
            it->timestamp = memberNode->timeOutCounter;
            return true;
        }
    }

    return false;
}


/**
 * FUNCTION NAME: nodeLoopOps
 *
 * DESCRIPTION: Check if any node hasn't responded within a timeout period and then delete
 * 				the nodes
 * 				Propagate your membership list
 */
void MP1Node::nodeLoopOps() {

	/*
	 * Your code goes here
	 */
    Address dstAddr;
    if(memberNode->pingCounter == 0)
    {
        // Increment number of heartbeats
        memberNode->heartbeat++;
        
        // Send heartbeat messages to all nodes
        std::random_shuffle ( memberNode->memberList.begin(), memberNode->memberList.end() );
        for (int i = 0; i<memberNode->memberList.size() && i < 3; ++i) {
            dstAddr = getAddress(memberNode->memberList[i].id, memberNode->memberList[i].port);
            sendMessage(HEARTBEAT, &dstAddr);
        }
        // Reset ping counter
        memberNode->pingCounter = TFAIL;
    }
    else {
        // Decrement ping counter
        memberNode->pingCounter--;
    }

    for (vector<MemberListEntry>::iterator m = memberNode->memberList.begin(); m != memberNode->memberList.end(); ) {
        if (memberNode->timeOutCounter - m->timestamp > TREMOVE) {
            dstAddr = getAddress(m->id, m->port);

            #ifdef DEBUGLOG
            log->logNodeRemove(&(memberNode->addr), &dstAddr);
            #endif

            m = memberNode->memberList.erase(m);
        } else {
            ++m;
        }
    }

    // Increment overall counter
    memberNode->timeOutCounter++;
    
    return;

}


void MP1Node::sendMessage(enum MsgTypes msgType, Address *destinationAddr){
    MessageHdr* msg;
    size_t msgsize;
    if(msgType == JOINREP){
        size_t memberListEntrySize = sizeof(int) + sizeof(short) + 2*sizeof(long);
        msgsize = sizeof(MessageHdr) + sizeof(int) + (memberNode->memberList.size() * memberListEntrySize);
        msg = (MessageHdr *) malloc(msgsize * sizeof(char));
        packTableMsg(msg);

    }else if(msgType == HEARTBEAT){
        msgsize = sizeof(MessageHdr) + sizeof(destinationAddr->addr) + sizeof(long) + 1;
        msg = (MessageHdr *) malloc(msgsize * sizeof(char));
        msg->msgType = msgType;
        memcpy((char*)(msg + 1), &memberNode->addr.addr, sizeof(memberNode->addr.addr));
        memcpy((char*)(msg + 1) + sizeof(memberNode->addr.addr), &memberNode->heartbeat, sizeof(long));
    }

    emulNet->ENsend(&memberNode->addr, destinationAddr, (char *)msg, msgsize);
    free(msg);
}




void MP1Node::packTableMsg(MessageHdr *msg) {
    // Serialize number of items
    int numberOfItems = memberNode->memberList.size();
    memcpy((char *)(msg + 1), &numberOfItems, sizeof(int));
        
    // Serialize member list entries
    int offset = sizeof(int);

    for(std::vector<MemberListEntry>::iterator it = memberNode->memberList.begin(); it != memberNode->memberList.end(); ++it) {     
        memcpy((char *)(msg + 1) + offset, &it->id, sizeof(int));
        offset += sizeof(int);
            
        memcpy((char *)(msg + 1) + offset, &it->port, sizeof(short));
        offset += sizeof(short);
            
        memcpy((char *)(msg + 1) + offset, &it->heartbeat, sizeof(long));
        offset += sizeof(long);
            
        memcpy((char *)(msg + 1) + offset, &it->timestamp, sizeof(long));
        offset += sizeof(long);
    }
}

// void MP1Node::getTableFromMsg(char *data) {
//     // Read message data
//     int numberOfItems;
//     memcpy(&numberOfItems, data + sizeof(MessageHdr), sizeof(int));
        
//     // Deserialize member list entries
//     int offset = sizeof(int);
        
//     for(int i = 0; i < numberOfItems; i++) {           
//         int id;
//         short port;
//         long heartbeat;
//         long timestamp;
            
//         memcpy(&id, data + sizeof(MessageHdr) + offset, sizeof(int));
//         offset += sizeof(int);
        
//         memcpy(&port, data + sizeof(MessageHdr) + offset, sizeof(short));
//         offset += sizeof(short);
            
//         memcpy(&heartbeat, data + sizeof(MessageHdr) + offset, sizeof(long));
//         offset += sizeof(long);
            
//         memcpy(&timestamp, data + sizeof(MessageHdr) + offset, sizeof(long));
//         offset += sizeof(long);
             
//         // Create and insert new entry
//         addMember(id, port, heartbeat, timestamp);
//     }
// }

/**
 * FUNCTION NAME: isNullAddress
 *
 * DESCRIPTION: Function checks if the address is NULL
 */
int MP1Node::isNullAddress(Address *addr) {
	return (memcmp(addr->addr, NULLADDR, 6) == 0 ? 1 : 0);
}

/**
 * FUNCTION NAME: getJoinAddress
 *
 * DESCRIPTION: Returns the Address of the coordinator
 */
Address MP1Node::getJoinAddress() {
    Address joinaddr;

    memset(&joinaddr, 0, sizeof(Address));
    *(int *)(&joinaddr.addr) = 1;
    *(short *)(&joinaddr.addr[4]) = 0;

    return joinaddr;
}

/**
 * FUNCTION NAME: getAddress
 *
 * DESCRIPTION: Returns the Address of the coordinator base on id and port
 */
Address MP1Node::getAddress(int id, short port) {
    Address a;

    memset(&a, 0, sizeof(Address));
    *(int *)(&a.addr) = id;
    *(short *)(&a.addr[4]) = port;

    return a;
}

/**
 * FUNCTION NAME: initMemberListTable
 *
 * DESCRIPTION: Initialize the membership list
 */
void MP1Node::initMemberListTable(Member *memberNode) {
	memberNode->memberList.clear();
}

/**
 * FUNCTION NAME: printAddress
 *
 * DESCRIPTION: Print the Address
 */
void MP1Node::printAddress(Address *addr)
{
    printf("%d.%d.%d.%d:%d \n",  addr->addr[0],addr->addr[1],addr->addr[2],
                                                       addr->addr[3], *(short*)&addr->addr[4]) ;    
}
