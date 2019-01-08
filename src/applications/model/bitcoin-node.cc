/**
 * This file contains the definitions of the functions declared in bitcoin-node.h
 */

#include "ns3/address.h"
#include "ns3/address-utils.h"
#include "ns3/log.h"
#include "ns3/inet-socket-address.h"
#include "ns3/inet6-socket-address.h"
#include "ns3/node.h"
#include "ns3/socket.h"
#include "ns3/udp-socket.h"
#include "ns3/simulator.h"
#include "ns3/socket-factory.h"
#include "ns3/packet.h"
#include "ns3/trace-source-accessor.h"
#include "ns3/udp-socket-factory.h"
#include "ns3/tcp-socket-factory.h"
#include "ns3/uinteger.h"
#include "ns3/double.h"
#include "bitcoin-node.h"
#include "../helper/bitcoin-node-helper.h"
#include <random>
#include <math.h>

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("BitcoinNode");

NS_OBJECT_ENSURE_REGISTERED (BitcoinNode);

int timeNotToCount = 20;

int RECON_HOP = 999;


int EstimateDifference(int setSize1, int setSize2, double multiplier) {
  return int(std::abs(setSize1 - setSize2) + multiplier * std::min(setSize1, setSize2));
}

int BitcoinNode::PoissonNextSend(int averageIntervalSeconds) {
    const uint64_t range_from  = 0;
    const uint64_t range_to    = 1ULL << 48;
    std::random_device                  rand_dev;
    std::mt19937                        generator(rand_dev() + GetNode()->GetId());
    std::uniform_int_distribution<uint64_t>  distr(range_from, range_to);
    auto bigRand = distr(generator);
    return (int)(log1p(bigRand * -0.0000000000000035527136788 /* -1/2^48 */) * averageIntervalSeconds * -1 + 0.5);
}

int BitcoinNode::PoissonNextSendIncoming(int averageIntervalSeconds) {
  auto now = Simulator::Now().GetSeconds();
  if (lastInvScheduled < now) {
    auto newDelay = PoissonNextSend(averageIntervalSeconds);
    lastInvScheduled = now + newDelay;
    return newDelay;
  }
  return lastInvScheduled - now;
}

TypeId
BitcoinNode::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::BitcoinNode")
    .SetParent<Application> ()
    .SetGroupName("Applications")
    .AddConstructor<BitcoinNode> ()
    .AddAttribute ("Local",
                   "The Address on which to Bind the rx socket.",
                   AddressValue (),
                   MakeAddressAccessor (&BitcoinNode::m_local),
                   MakeAddressChecker ())
    .AddAttribute ("Protocol",
                   "The type id of the protocol to use for the rx socket.",
                   TypeIdValue (UdpSocketFactory::GetTypeId ()),
                   MakeTypeIdAccessor (&BitcoinNode::m_tid),
                   MakeTypeIdChecker ())
    .AddAttribute ("InvTimeoutMinutes",
				   "The timeout of inv messages in minutes",
                   TimeValue (Minutes (20)),
                   MakeTimeAccessor (&BitcoinNode::m_invTimeoutMinutes),
                   MakeTimeChecker())
    .AddTraceSource ("Rx",
                     "A packet has been received",
                     MakeTraceSourceAccessor (&BitcoinNode::m_rxTrace),
                     "ns3::Packet::AddressTracedCallback")
  ;
  return tid;
}

BitcoinNode::BitcoinNode (void) : m_bitcoinPort (8333), m_secondsPerMin(60), m_countBytes (4), m_bitcoinMessageHeader (90),
                                  m_inventorySizeBytes (36), m_getHeadersSizeBytes (72), m_headersSizeBytes (81),
                                  m_averageTransactionSize (522.4), m_timeToRun(0), m_mode(REGULAR)
{
  NS_LOG_FUNCTION (this);
  m_socket = 0;
  heardTotal = 0;
  firstTimeHops = std::vector<int>(1024);
  m_numberOfPeers = m_peersAddresses.size();
  m_numInvsSent = 0;
  txCreator = false;
  voidReconciliations = 0;
}

BitcoinNode::~BitcoinNode(void)
{
  NS_LOG_FUNCTION (this);
}

Ptr<Socket>
BitcoinNode::GetListeningSocket (void) const
{
  NS_LOG_FUNCTION (this);
  return m_socket;
}


std::vector<Ipv4Address>
BitcoinNode::GetPeersAddresses (void) const
{
  NS_LOG_FUNCTION (this);
  return m_peersAddresses;
}


void
BitcoinNode::SetPeersAddresses (const std::vector<Ipv4Address> &peers)
{
  NS_LOG_FUNCTION (this);
  m_peersAddresses = peers;
  m_numberOfPeers = m_peersAddresses.size();
}


void
BitcoinNode::SetPeersDownloadSpeeds (const std::map<Ipv4Address, double> &peersDownloadSpeeds)
{
  NS_LOG_FUNCTION (this);
  m_peersDownloadSpeeds = peersDownloadSpeeds;
}


void
BitcoinNode::SetPeersUploadSpeeds (const std::map<Ipv4Address, double> &peersUploadSpeeds)
{
  NS_LOG_FUNCTION (this);
  m_peersUploadSpeeds = peersUploadSpeeds;
}

void
BitcoinNode::SetNodeInternetSpeeds (const nodeInternetSpeeds &internetSpeeds)
{
  NS_LOG_FUNCTION (this);

  m_downloadSpeed = internetSpeeds.downloadSpeed * 1000000 / 8 ;
  m_uploadSpeed = internetSpeeds.uploadSpeed * 1000000 / 8 ;
}


void
BitcoinNode::SetNodeStats (nodeStatistics *nodeStats)
{
  NS_LOG_FUNCTION (this);
  m_nodeStats = nodeStats;
};

void
BitcoinNode::SetProperties (uint64_t timeToRun, enum ModeType mode,
    int systemId, std::vector<Ipv4Address> outPeers, ProtocolSettings protocolSettings)
{
  NS_LOG_FUNCTION (this);
  m_timeToRun = timeToRun;
  m_mode = mode;
  m_systemId = systemId;
  m_outPeers = outPeers;
  m_protocolSettings = protocolSettings;
  m_protocolSettings.reconciliationIntervalSeconds *= (m_peersAddresses.size() / m_outPeers.size());

  m_prevA = A_ESTIMATOR;

  for (auto peer: m_peersAddresses) {
    if (std::find(m_outPeers.begin(), m_outPeers.end(), peer) == m_outPeers.end())
      m_inPeers.push_back(peer);
  }

  if (m_protocolSettings.reconciliationMode != RECON_OFF)
  {
      for (auto peer: m_peersAddresses)
      {
          std::vector<int> s;
          m_peerReconciliationSets.insert(std::pair<Ipv4Address,std::vector<int>>(peer, s));
          if (std::find(m_outPeers.begin(), m_outPeers.end(), peer) != m_outPeers.end()) {
            m_reconcilePeers.push_back(peer);
            // m_reconciliationHistory[peer] = std::vector<int>(4, 0);
          }
      }
  }
  peerStatistics peerstats;
  peerstats.numUsefulInvReceived = 0;
  peerstats.numUselessInvReceived = 0;
  peerstats.numGetDataReceived = 0;
  peerstats.numGetDataSent = 0;
  peerstats.connectionLength = 0;
  peerstats.usefulInvRate = 0;
  for(auto peer : m_peersAddresses) {
    m_peerStatistics.insert(std::pair<Ipv4Address, peerStatistics>(peer, peerstats));
  }
}

void
BitcoinNode::DoDispose (void)
{
  NS_LOG_FUNCTION (this);
  m_socket = 0;

  // chain up
  Application::DoDispose ();
}


// Application Methods
void
BitcoinNode::StartApplication ()    // Called at time specified by Start
{
  NS_LOG_FUNCTION (this);
  // Create the socket if not already

  srand(time(NULL) + GetNode()->GetId());
  NS_LOG_INFO ("Node " << GetNode()->GetId() << ": download speed = " << m_downloadSpeed << " B/s");
  NS_LOG_INFO ("Node " << GetNode()->GetId() << ": upload speed = " << m_uploadSpeed << " B/s");
  NS_LOG_INFO ("Node " << GetNode()->GetId() << ": m_numberOfPeers = " << m_numberOfPeers);
  NS_LOG_INFO ("Node " << GetNode()->GetId() << ": m_invTimeoutMinutes = " << m_invTimeoutMinutes.GetMinutes() << "mins");

  NS_LOG_INFO ("Node " << GetNode()->GetId() << ": My peers are");

  for (auto it = m_peersAddresses.begin(); it != m_peersAddresses.end(); it++)
    NS_LOG_INFO("\t" << *it);

  double currentMax = 0;

  if (!m_socket)
  {
    m_socket = Socket::CreateSocket (GetNode (), m_tid);
    m_socket->Bind (m_local);
    m_socket->Listen ();
    m_socket->ShutdownSend ();
    if (addressUtils::IsMulticast (m_local))
    {
      Ptr<UdpSocket> udpSocket = DynamicCast<UdpSocket> (m_socket);
      if (udpSocket)
      {
        // equivalent to setsockopt (MCAST_JOIN_GROUP)
        udpSocket->MulticastJoinGroup (0, m_local);
      }
      else
      {
        NS_FATAL_ERROR ("Error: joining multicast on a non-UDP socket");
      }
    }
  }

  m_socket->SetRecvCallback (MakeCallback (&BitcoinNode::HandleRead, this));
  m_socket->SetAcceptCallback (
    MakeNullCallback<bool, Ptr<Socket>, const Address &> (),
    MakeCallback (&BitcoinNode::HandleAccept, this));
  m_socket->SetCloseCallbacks (
    MakeCallback (&BitcoinNode::HandlePeerClose, this),
    MakeCallback (&BitcoinNode::HandlePeerError, this));

  NS_LOG_DEBUG ("Node " << GetNode()->GetId() << ": Before creating sockets");
  for (std::vector<Ipv4Address>::const_iterator i = m_peersAddresses.begin(); i != m_peersAddresses.end(); ++i)
  {
    m_peersSockets[*i] = Socket::CreateSocket (GetNode (), TcpSocketFactory::GetTypeId ());
    m_peersSockets[*i]->Connect (InetSocketAddress (*i, m_bitcoinPort));
  }
  NS_LOG_DEBUG ("Node " << GetNode()->GetId() << ": After creating sockets");

  m_nodeStats->nodeId = GetNode()->GetId();
  m_nodeStats->systemId = GetNode()->GetId();

  m_nodeStats->invReceivedMessages = 0;
  m_nodeStats->reconInvReceivedMessages = 0;
  m_nodeStats->uselessInvReceivedMessages = 0;
  m_nodeStats->reconUselessInvReceivedMessages = 0;
  m_nodeStats->txCreated = 0;
  m_nodeStats->connections = m_peersAddresses.size();

  m_nodeStats->firstSpySuccess = 0;
  m_nodeStats->txReceived = 0;

  m_nodeStats->systemId = m_systemId;

  m_nodeStats->reconcils = 0;

  m_nodeStats->mode = m_mode;
  m_nodeStats->onTheFlyCollisions = 0;

  if (m_nodeStats->nodeId == 1) {
    LogTime();
  }

  AnnounceMode();

  if (m_mode == BLACK_HOLE)
    return;

  if (m_protocolSettings.protocol == DANDELION_MAPPING) {
    RotateDandelionDestinations();
  }

  if (m_protocolSettings.reconciliationMode != RECON_OFF) {
    int nextReconciliation = 10;
    Simulator::Schedule (Seconds(nextReconciliation), &BitcoinNode::ReconcileWithPeer, this);
  }
}


void BitcoinNode::LogTime() {
  std::cout << Simulator::Now().GetSeconds() << std::endl;
  if (m_timeToRun < Simulator::Now().GetSeconds()) {
    return;
  }
  Simulator::Schedule (Seconds(10), &BitcoinNode::LogTime, this);
}

void BitcoinNode::RotateDandelionDestinations() {
  // for (auto peer: m_peersAddresses){
  //   m_dandelionDestinations[peer].clear();
  //   std::vector<Ipv4Address> outPeersCopy(m_outPeers);
  //   for (int i = 0; i < m_protocolSettings.lowfanoutOrderOut; i++) {
  //     auto peer = ChooseFromPeers(outPeersCopy);
  //     m_dandelionDestinations[peer].push_back(peer);
  //     outPeersCopy.erase(std::find(outPeersCopy.begin(), outPeersCopy.end(), peer));
  //   }
  //
  //   std::vector<Ipv4Address> inPeersCopy(m_inPeers);
  //   for (int i = 0; i < m_protocolSettings.lowfanoutOrderInPercent * m_inPeers.size(); i++) {
  //     auto peer = ChooseFromPeers(inPeersCopy);
  //     m_dandelionDestinations[peer].push_back(peer);
  //     inPeersCopy.erase(std::find(inPeersCopy.begin(), inPeersCopy.end(), peer));
  //   }
  // }
  //
  // if (m_timeToRun < Simulator::Now().GetSeconds()) {
  //   return;
  // }
  Simulator::Schedule (Seconds(DANDELION_ROTATION_SECONDS), &BitcoinNode::RotateDandelionDestinations, this);
}



void
BitcoinNode::StopApplication ()     // Called at time specified by Stop
{
  NS_LOG_FUNCTION (this);

  for (std::vector<Ipv4Address>::iterator i = m_peersAddresses.begin(); i != m_peersAddresses.end(); ++i) //close the outgoing sockets
  {
    m_peersSockets[*i]->Close ();
  }


  if (m_socket)
  {
    m_socket->Close ();
    m_socket->SetRecvCallback (MakeNullCallback<void, Ptr<Socket> > ());
  }

  NS_LOG_WARN ("\n\nBITCOIN NODE " << GetNode ()->GetId () << ":");
}

void
BitcoinNode::ReconcileWithPeer(void) {
    assert(m_reconcilePeers.size() != 0);

    const uint8_t delimiter[] = "#";
    Ipv4Address peer;
    if (m_protocolSettings.reconciliationMode == TIME_BASED) {
      peer = m_reconcilePeers.front();
      if (m_protocolSettings.bhDetection && peersMode[peer] == BLACK_HOLE) {
        m_reconcilePeers.pop_front();
        peer = m_reconcilePeers.front();
      }
      m_reconcilePeers.pop_front();
      m_reconcilePeers.push_back(peer);
    } else if (m_protocolSettings.reconciliationMode = SET_SIZE_BASED) {
      bool peerFound = false;
      for (auto curPeer: m_reconcilePeers) {
        size_t setSize = m_peerReconciliationSets[peer].size();
        if (setSize > RECON_MAX_SET_SIZE) {
          peer = curPeer;
          peerFound = true;
          break;
        }
      }
      if (!peerFound) {
          Simulator::Schedule (Seconds(m_protocolSettings.reconciliationIntervalSeconds), &BitcoinNode::ReconcileWithPeer, this);
          return;
      }
    }
    size_t set_size = m_peerReconciliationSets[peer].size();

    rapidjson::Document reconcileData;
    rapidjson::Value value;
    value = RECONCILE_TX_REQUEST;
    reconcileData.SetObject();
    reconcileData.AddMember("message", value, reconcileData.GetAllocator());

    rapidjson::Value setSize;

    setSize.SetInt(set_size);

    reconcileData.AddMember("setSize", setSize, reconcileData.GetAllocator());

    rapidjson::StringBuffer reconcileInfo;
    rapidjson::Writer<rapidjson::StringBuffer> reconcileWriter(reconcileInfo);
    reconcileData.Accept(reconcileWriter);

    m_peersSockets[peer]->Send(reinterpret_cast<const uint8_t*>(reconcileInfo.GetString()), reconcileInfo.GetSize(), 0);
    m_peersSockets[peer]->Send(delimiter, 1, 0);

    if (m_timeToRun < Simulator::Now().GetSeconds()) {
      return;
    }
    Simulator::Schedule (Seconds(m_protocolSettings.reconciliationIntervalSeconds), &BitcoinNode::ReconcileWithPeer, this);
}

Ipv4Address
BitcoinNode::ChooseFromPeers(std::vector<Ipv4Address> peers)
{
    if (m_peerStatistics.empty())
        NS_FATAL_ERROR ("Error: m_peerStatistics is empty");
    std::random_device rd;
    std::mt19937 eng(rd());
    std::uniform_int_distribution<> distr(0, peers.size() - 1);
    int index = distr(eng);
    return peers.at(index);
}

void
BitcoinNode::AnnounceMode (void)
{
  int count = 0;
  const uint8_t delimiter[] = "#";

  for (std::vector<Ipv4Address>::const_iterator i = m_peersAddresses.begin(); i != m_peersAddresses.end(); ++i)
  {
    rapidjson::Document modeData;

    rapidjson::Value value;
    value = MODE;
    modeData.SetObject();

    modeData.AddMember("message", value, modeData.GetAllocator());

    rapidjson::Value modeValue;
    modeValue.SetInt(m_mode);


    modeData.AddMember("mode", modeValue, modeData.GetAllocator());


    rapidjson::StringBuffer modeInfo;
    rapidjson::Writer<rapidjson::StringBuffer> modeWriter(modeInfo);
    modeData.Accept(modeWriter);

    m_peersSockets[*i]->Send (reinterpret_cast<const uint8_t*>(modeInfo.GetString()), modeInfo.GetSize(), 0);
    m_peersSockets[*i]->Send(delimiter, 1, 0);
  }

  if (m_mode == TX_EMITTER)
    Simulator::Schedule (Seconds(5), &BitcoinNode::ScheduleNextTransactionEvent, this);

}

int MurmurHash3Mixer(int key) // TODO a better hash function
{
  key ^= (key >> 13);
  key *= 0xff51afd7ed558ccd;
  key ^= (key >> 13);
  key *= 0xc4ceb9fe1a85ec53;
  key ^= (key >> 13);
  return key;
}

void
BitcoinNode::ScheduleNextTransactionEvent (void)
{
  NS_LOG_FUNCTION (this);

  // 7 tx/s
  int currentMinute = Simulator::Now().GetSeconds() / 60;
  int revProbability = TX_EMITTERS/transactionRates[currentMinute];
  bool emit = (rand() % revProbability) == 0;

  // Do not emit transactions which will be never reconciled in the network
  if (m_timeToRun < Simulator::Now().GetSeconds() + timeNotToCount)
    return;

  if (emit)
    EmitTransaction();
  Simulator::Schedule (Seconds(1), &BitcoinNode::ScheduleNextTransactionEvent, this);
}

void
BitcoinNode::EmitTransaction (void)
{
  NS_LOG_FUNCTION (this);
  int nodeId = GetNode()->GetId();
  m_nodeStats->txCreated++;

  int transactionId = nodeId*1000000 + m_nodeStats->txCreated;
  NS_LOG_INFO("E");
  auto myself = InetSocketAddress::ConvertFrom(m_local).GetIpv4();
  NS_LOG_INFO("F");

  AdvertiseTransactionInvWrapper(myself, transactionId, 0);
  SaveTxData(transactionId, myself);
}


void
BitcoinNode::HandleRead (Ptr<Socket> socket)
{
  NS_LOG_FUNCTION (this << socket);
  Ptr<Packet> packet;
  Address from;

  while ((packet = socket->RecvFrom (from)))
  {
      if (packet->GetSize () == 0)
      { //EOF
         break;
      }

      if (m_mode == BLACK_HOLE)
        return;

      if (InetSocketAddress::IsMatchingType (from))
      {
        /**
         * We may receive more than one packets simultaneously on the socket,
         * so we have to parse each one of them.
         */
        std::string delimiter = "#";
        std::string parsedPacket;
        size_t pos = 0;
        char *packetInfo = new char[packet->GetSize () + 1];
        std::ostringstream totalStream;

        packet->CopyData (reinterpret_cast<uint8_t*>(packetInfo), packet->GetSize ());
        packetInfo[packet->GetSize ()] = '\0'; // ensure that it is null terminated to avoid bugs

        /**
         * Add the buffered data to complete the packet
         */
        totalStream << m_bufferedData[from] << packetInfo;
        std::string totalReceivedData(totalStream.str());
        NS_LOG_INFO("Node " << GetNode ()->GetId () << " Total Received Data: " << totalReceivedData);

        while ((pos = totalReceivedData.find(delimiter)) != std::string::npos)
        {
          parsedPacket = totalReceivedData.substr(0, pos);
          NS_LOG_INFO("Node " << GetNode ()->GetId () << " Parsed Packet: " << parsedPacket);

          rapidjson::Document d;
          d.Parse(parsedPacket.c_str());

          if(!d.IsObject())
          {
            NS_LOG_WARN("The parsed packet is corrupted");
            totalReceivedData.erase(0, pos + delimiter.length());
            continue;
          }

          rapidjson::StringBuffer buffer;
          rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
          d.Accept(writer);

          Ipv4Address peer = InetSocketAddress::ConvertFrom(from).GetIpv4();

          NS_LOG_INFO ("At time "  << Simulator::Now ().GetSeconds ()
                        << "s bitcoin node " << GetNode ()->GetId () << " received "
                        <<  packet->GetSize () << " bytes from "
                        << InetSocketAddress::ConvertFrom(from).GetIpv4 ()
                        << " port " << InetSocketAddress::ConvertFrom (from).GetPort ()
                        << " with info = " << buffer.GetString());


          switch (d["message"].GetInt())
          {
            case MODE:
            {
              ModeType mode = ModeType(d["mode"].GetInt());
              peersMode[peer] = mode;
              break;
            }
            case RECONCILE_TX_REQUEST:
            {
                size_t set =  d["setSize"].GetInt();
                auto delay = PoissonNextSend(2);
                Simulator::Schedule (Seconds(delay), &BitcoinNode::RespondToReconciliationRequest, this, peer);
                break;
            }
            case RECONCILE_TX_RESPONSE:
            {
                std::set<int> nodeBtransactions;
                int iMissCounter = 0;
                std::vector<int> peerSet = std::vector<int>(m_peerReconciliationSets[peer]);
                int mySubSetSize[SUB_SETS] = {0};
                int hisSubSetSize[SUB_SETS] = {0};
                for (rapidjson::Value::ConstValueIterator itr = d["transactions"].Begin(); itr != d["transactions"].End(); ++itr) {
                    int txId = itr->GetInt();
                    hisSubSetSize[MurmurHash3Mixer(txId) % SUB_SETS]++;
                    peersKnowTx[txId].push_back(peer);
                    nodeBtransactions.insert(txId);
                    if (std::find(peerSet.begin(), peerSet.end(), txId) != peerSet.end()) {
                      continue;
                    } else {
                      if (std::find(knownTxHashes.begin(), knownTxHashes.end(), txId) != knownTxHashes.end()) {
                        continue;
                      }
                    }
                    iMissCounter++;
                    SaveTxData(txId, peer);
                    // AdvertiseTransactionInvWrapper(peer, txId, 0);
                }
                int heMissCounter = 0;
                for (int it: peerSet)
                {
                    mySubSetSize[MurmurHash3Mixer(it) % SUB_SETS]++;
                    // m_peerReconciliationSets[peer].clear();
                    if (std::find(nodeBtransactions.begin(), nodeBtransactions.end(), it) == nodeBtransactions.end())
                    {
                        // Do not inv to out peer, it will learn it later ???
                        // Due to assymetry in the network
                        // m_peerReconciliationSets[peer].push_back(it);
                        Simulator::Schedule (Seconds(0.1), &BitcoinNode::SendInvToNode, this, peer, it, RECON_HOP);
                        heMissCounter++;
                    }
                }
                m_peerReconciliationSets[peer].clear();
                int totalDiff = iMissCounter + heMissCounter;
                if (m_timeToRun < Simulator::Now().GetSeconds() + timeNotToCount)
                  break;

                // int estimatedDiff = (EstimateDifference(peerSet.size(), d["transactions"].Size(), 0.1) * m_protocolSettings.qEstimationMultiplier +
                //   m_reconciliationHistory[peer] * (1-m_protocolSettings.qEstimationMultiplier));

                // int estimatedDiff = 0;
                // for (int i = 0; i < SUB_SETS; i++) {
                //   estimatedDiff += EstimateDifference(mySubSetSize[i], hisSubSetSize[i], 0.1);
                // }

                int mySetSize = peerSet.size();
                int hisSetSize = d["transactions"].Size();
                int estimatedDiff = EstimateDifference(mySetSize, hisSetSize, m_prevA) + m_protocolSettings.qEstimationMultiplier;
                if (mySetSize * hisSetSize != 0 && estimatedDiff >= mySetSize + hisSetSize)
                  m_prevA = (totalDiff-std::abs(mySetSize - hisSetSize)) / std::min(mySetSize, hisSetSize);

                reconcilItem item;
                item.setInSize = d["transactions"].Size();
                item.setOutSize = peerSet.size();
                item.diffSize = totalDiff;
                item.estimatedDiff = estimatedDiff;
                item.nodeId = m_nodeStats->nodeId;
                m_nodeStats->reconcilData.push_back(item);
                m_nodeStats->reconcils++;


                // m_reconciliationHistory[peer] = totalDiff;

                break;
            }
            case INV:
            {
              std::vector<std::string>            requestTxs;
              for (int j=0; j<d["inv"].Size(); j++)
              {
                int   parsedInv = d["inv"][j].GetInt();
                int   hopNumber = d["hop"].GetInt();
                if (std::find(peersKnowTx[parsedInv].begin(), peersKnowTx[parsedInv].end(), peer) != peersKnowTx[parsedInv].end())
                  m_nodeStats->onTheFlyCollisions++;
                if (hopNumber == RECON_HOP ) {
                  m_nodeStats->reconInvReceivedMessages++;
                } else {
                  m_nodeStats->invReceivedMessages++;
                }
                peersKnowTx[parsedInv].push_back(peer);
                if (m_protocolSettings.reconciliationMode != RECON_OFF) {
                  RemoveFromReconciliationSets(parsedInv, peer);
                }

                if (std::find(knownTxHashes.begin(), knownTxHashes.end(), parsedInv) != knownTxHashes.end()) {
                    // loop handling
                    if (hopNumber == RECON_HOP ) {
                      m_nodeStats->reconUselessInvReceivedMessages++;
                    } else {
                      m_nodeStats->uselessInvReceivedMessages++;
                    }
                    // if (std::find(loopHistory.begin(), loopHistory.end(), parsedInv) == loopHistory.end() &&
                    //   m_protocolSettings.loopAccommodation == 1) {
                    //   loopHistory.push_back(parsedInv);
                    //   AdvertiseTransactionInvWrapper(from, parsedInv, hopNumber + 1);
                    // }
                    continue;
                } else {
                  SaveTxData(parsedInv, peer);
                  AdvertiseTransactionInvWrapper(from, parsedInv, hopNumber + 1);
                }
              }
              break;
            }
            default:
              NS_LOG_INFO ("Default");
              break;
          }

          totalReceivedData.erase(0, pos + delimiter.length());
        }

        /**
        * Buffer the remaining data
        */
        NS_LOG_INFO("741");
        m_bufferedData[from] = totalReceivedData;
        NS_LOG_INFO("743");
        delete[] packetInfo;
        NS_LOG_INFO("745");
      }
      else if (Inet6SocketAddress::IsMatchingType (from))
      {
        NS_LOG_INFO ("At time " << Simulator::Now ().GetSeconds ()
                     << "s bitcoin node " << GetNode ()->GetId () << " received "
                     <<  packet->GetSize () << " bytes from "
                     << Inet6SocketAddress::ConvertFrom(from).GetIpv6 ()
                     << " port " << Inet6SocketAddress::ConvertFrom (from).GetPort ());
      }
      NS_LOG_INFO("755");
      m_rxTrace (packet, from);
      NS_LOG_INFO("757");
  }
}

void
BitcoinNode::AdvertiseTransactionInvWrapper (Address from, const int transactionHash, int hopNumber)
{
    NS_LOG_INFO("A");
    Ipv4Address ipv4From;
    if (hopNumber != 0)
      ipv4From = InetSocketAddress::ConvertFrom(from).GetIpv4();
    NS_LOG_INFO("B");

    switch(m_protocolSettings.protocol)
    {
        case STANDARD_PROTOCOL:
        {
            AdvertiseNewTransactionInvStandard(ipv4From, transactionHash, hopNumber);
            break;
        }
        case PREFERRED_OUT_DESTINATIONS:
        {
			       AdvertiseNewTransactionInv(ipv4From, transactionHash, hopNumber, m_outPeers, m_protocolSettings.lowfanoutOrderOut);
             break;
        }
        case PREFERRED_ALL_DESTINATIONS:
        {
             AdvertiseNewTransactionInv(ipv4From, transactionHash, hopNumber, m_outPeers, m_protocolSettings.lowfanoutOrderOut);
             AdvertiseNewTransactionInv(ipv4From, transactionHash, hopNumber, m_inPeers, m_protocolSettings.lowfanoutOrderInPercent);
			       // AdvertiseNewTransactionInv(ipv4From, transactionHash, hopNumber, m_inPeers,
             //   floor(m_protocolSettings.lowfanoutOrderInPercent * 1.0 / 100.0 * m_inPeers.size()));
             break;
        }
        case DANDELION_MAPPING:
        {
            // AdvertiseNewTransactionInv(ipv4From, transactionHash, hopNumber, m_dandelionDestinations[ipv4From],
            //    m_dandelionDestinations[ipv4From].size());
             break;
        }
    }
}


void
BitcoinNode::RespondToReconciliationRequest(Ipv4Address from)
{
  NS_LOG_FUNCTION (this);
  Ipv4Address peer = InetSocketAddress::ConvertFrom(from).GetIpv4();

  rapidjson::Document reconcileData;
  reconcileData.SetObject();

  rapidjson::Value msg;
  rapidjson::Value txArray(rapidjson::kArrayType);

  rapidjson::Document::AllocatorType& allocator = reconcileData.GetAllocator();

  msg = RECONCILE_TX_RESPONSE;
  reconcileData.AddMember("message", msg, allocator);

  for (int it: m_peerReconciliationSets[peer]) {
      rapidjson::Value txhash;
      txhash.SetInt(it);
      txArray.PushBack(txhash,allocator);
      peersKnowTx[it].push_back(peer);
  }
  reconcileData.AddMember("transactions", txArray, allocator);
  rapidjson::StringBuffer reconcileInfo;
  rapidjson::Writer<rapidjson::StringBuffer> reconcileWriter(reconcileInfo);
  reconcileData.Accept(reconcileWriter);

  const uint8_t delimiter[] = "#";
  m_peersSockets[peer]->Send(reinterpret_cast<const uint8_t*>(reconcileInfo.GetString()), reconcileInfo.GetSize(), 0);
  m_peersSockets[peer]->Send(delimiter, 1, 0);

  m_peerReconciliationSets[peer].clear();
}


void
BitcoinNode::AdvertiseNewTransactionInvStandard(Ipv4Address from, const int transactionHash, int hopNumber)
{
  NS_LOG_FUNCTION (this);
  assert(m_protocolSettings.protocol == STANDARD_PROTOCOL);
  for (Ipv4Address i: m_peersAddresses)
  {
    if (i != from)
    {
      double delay = 0.1;
      if (std::find(m_outPeers.begin(), m_outPeers.end(), i) == m_outPeers.end())
        delay += PoissonNextSend(m_protocolSettings.invIntervalSeconds);
      else
        delay += PoissonNextSendIncoming(m_protocolSettings.invIntervalSeconds >> 1);
      Simulator::Schedule (Seconds(delay), &BitcoinNode::SendInvToNode, this, i, transactionHash, hopNumber);
    }
  }
}

void
BitcoinNode::AdvertiseNewTransactionInv(Ipv4Address from, const int transactionHash, int hopNumber, std::vector<Ipv4Address> peers, int peersToRelayTo)
{
    NS_LOG_FUNCTION (this);
    if (peers.size() < peersToRelayTo)
      return;
    // does not stuck in endless loop
    int tries = peers.size();
    while (peersToRelayTo > 0) {
      auto preferredPeer = ChooseFromPeers(peers);
      bool fromPeer = (preferredPeer == from);
      // avoid unexpected behaviour due to unordered messages
      bool recentlyReconciled = (preferredPeer == m_reconcilePeers.front() || preferredPeer == m_reconcilePeers.back());
      bool alreadyKnows = std::find(peersKnowTx[transactionHash].begin(), peersKnowTx[transactionHash].end(), preferredPeer) != peersKnowTx[transactionHash].end();
      if (fromPeer || recentlyReconciled || alreadyKnows) {
        tries--;
        if (tries == 0)
          break;
        else
          continue;
      }
      double delay = 0.1;
      delay += PoissonNextSend(m_protocolSettings.invIntervalSeconds);
      Simulator::Schedule (Seconds(delay), &BitcoinNode::SendInvToNode, this, preferredPeer, transactionHash, hopNumber);
      peersToRelayTo--;
      tries = peers.size();
    }
}

void
BitcoinNode::SendInvToNode(Ipv4Address receiver, const int transactionHash, int hopNumber) {
  bool alreadyKnows = std::find(peersKnowTx[transactionHash].begin(), peersKnowTx[transactionHash].end(), receiver) != peersKnowTx[transactionHash].end();

  if (alreadyKnows)
    return;

  rapidjson::Document inv;
  inv.SetObject();
  rapidjson::Value value;

  value = INV;
  inv.AddMember("message", value, inv.GetAllocator());


  rapidjson::Value array(rapidjson::kArrayType);
  value.SetInt(transactionHash);
  array.PushBack(value, inv.GetAllocator());
  inv.AddMember("inv", array, inv.GetAllocator());

  value = hopNumber;
  inv.AddMember("hop", value, inv.GetAllocator());


  rapidjson::StringBuffer invInfo;
  rapidjson::Writer<rapidjson::StringBuffer> invWriter(invInfo);
  inv.Accept(invWriter);
  const uint8_t delimiter[] = "#";
  m_peersSockets[receiver]->Send (reinterpret_cast<const uint8_t*>(invInfo.GetString()), invInfo.GetSize(), 0);
  m_peersSockets[receiver]->Send (delimiter, 1, 0);

  peersKnowTx[transactionHash].push_back(receiver);
  RemoveFromReconciliationSets(transactionHash, receiver);
}

void
BitcoinNode::SendMessage(enum Messages receivedMessage,  enum Messages responseMessage, rapidjson::Document &d, Address &outgoingAddress)
{
  NS_LOG_FUNCTION (this);

  const uint8_t delimiter[] = "#";

  rapidjson::StringBuffer buffer;
  rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);



  d["message"].SetInt(responseMessage);
  d.Accept(writer);

  NS_LOG_INFO("C");
  Ipv4Address outgoingIpv4Address = InetSocketAddress::ConvertFrom(outgoingAddress).GetIpv4 ();
  NS_LOG_INFO("D");
  std::map<Ipv4Address, Ptr<Socket>>::iterator it = m_peersSockets.find(outgoingIpv4Address);

  if (it == m_peersSockets.end()) //Create the socket if it doesn't exist
  {
    m_peersSockets[outgoingIpv4Address] = Socket::CreateSocket (GetNode (), TcpSocketFactory::GetTypeId ());
    m_peersSockets[outgoingIpv4Address]->Connect (InetSocketAddress (outgoingIpv4Address, m_bitcoinPort));
  }

  m_peersSockets[outgoingIpv4Address]->Send (reinterpret_cast<const uint8_t*>(buffer.GetString()), buffer.GetSize(), 0);
  m_peersSockets[outgoingIpv4Address]->Send (delimiter, 1, 0);
}

void BitcoinNode::SaveTxData(int txId, Ipv4Address from) {
  assert(std::find(knownTxHashes.begin(), knownTxHashes.end(), txId) == knownTxHashes.end());
  txRecvTime txTime;
  txTime.nodeId = GetNode()->GetId();
  txTime.txHash = txId;
  txTime.txTime = Simulator::Now().GetSeconds();
  m_nodeStats->txReceivedTimes.push_back(txTime);
  knownTxHashes.push_back(txId);
  m_nodeStats->txReceived++;
  if (m_protocolSettings.reconciliationMode != RECON_OFF) {
    // Simulator::Schedule (Seconds(3), &BitcoinNode::AddToReconciliationSets, this, txId, from);
    AddToReconciliationSets(txId, from);
  }
}

void BitcoinNode::AddToReconciliationSets(int txId, Ipv4Address from) {
  if (m_timeToRun < Simulator::Now().GetSeconds() + timeNotToCount)
    return;

  // std::cout << "Node " << m_nodeStats->nodeId << " adds tx: " << txId << "from peer" << from << std::endl;
  for (std::vector<Ipv4Address>::const_iterator i = m_peersAddresses.begin(); i != m_peersAddresses.end(); ++i)
  {
    if (*i == from || peersMode[*i] == BLACK_HOLE) {
      continue;
    }
    m_peerReconciliationSets[*i].push_back(txId);
  }
}

void BitcoinNode::RemoveFromReconciliationSets(int txId, Ipv4Address from) {
  auto item = std::find(m_peerReconciliationSets[from].begin(), m_peerReconciliationSets[from].end(), txId);
  if (item != m_peerReconciliationSets[from].end())
    m_peerReconciliationSets[from].erase(item);
}


void
BitcoinNode::HandlePeerClose (Ptr<Socket> socket)
{
  NS_LOG_FUNCTION (this << socket);
}

void BitcoinNode::HandlePeerError (Ptr<Socket> socket)
{
  NS_LOG_FUNCTION (this << socket);
}


void
BitcoinNode::HandleAccept (Ptr<Socket> s, const Address& from)
{
  NS_LOG_FUNCTION (this << s << from);
  s->SetRecvCallback (MakeCallback (&BitcoinNode::HandleRead, this));
}


} // Namespace ns3
