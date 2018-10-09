/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <fstream>
#include <time.h>
#include <sys/time.h>
#include <stddef.h>
#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
#include "ns3/point-to-point-layout-module.h"
#include "ns3/mpi-interface.h"
#include <climits>

#define MPI_TEST

#ifdef NS3_MPI
#include <mpi.h>
#endif

using namespace ns3;

double get_wall_time();
int GetNodeIdByIpv4 (Ipv4InterfaceContainer container, Ipv4Address addr);
void PrintStatsForEachNode (nodeStatistics *stats, int totalNodes, int publicIPNodes, int blackHoles);
void PrintBitcoinRegionStats (uint32_t *bitcoinNodesRegions, uint32_t totalNodes);
void CollectTxData(nodeStatistics *stats, int totalNoNodes,
   int systemId, int systemCount, int nodesInSystemId0, BitcoinTopologyHelper bitcoinTopologyHelper);
void CollectReconcilData(nodeStatistics *stats, int totalNoNodes,
  int systemId, int systemCount, int nodesInSystemId0, BitcoinTopologyHelper bitcoinTopologyHelper);
int PoissonDistribution(int value);
std::vector<int> generateTxCreateList(int n, int nodes);


NS_LOG_COMPONENT_DEFINE ("MyMpiTest");

std::map<int, int> nodesModes;

int
main (int argc, char *argv[])
{
  std::cout << "Start \n";

  bool nullmsg = false;
  bool testScalability = false;
  int invTimeoutMins = -1;
  double tStart = get_wall_time(), tStartSimulation, tFinish;
  const int secsPerMin = 60;
  const uint16_t bitcoinPort = 8333;
  int start = 0;
//
  int totalNoNodes = 16;
  int minConnectionsPerNode = -1;
  int maxConnectionsPerNode = -1;

  uint32_t protocol;


  uint64_t simulTime = 1024;
  int publicIPNodes, blocksOnlyPrivateIpNodes;

  double stop, overlap;

//
  Ipv4InterfaceContainer                               ipv4InterfaceContainer;
  std::map<uint32_t, std::vector<Ipv4Address>>         nodesConnections;
  std::map<uint32_t, std::map<Ipv4Address, double>>    peersDownloadSpeeds;
  std::map<uint32_t, std::map<Ipv4Address, double>>    peersUploadSpeeds;
  std::map<uint32_t, nodeInternetSpeeds>               nodesInternetSpeeds;
  int                                                  nodesInSystemId0 = 0;

  Time::SetResolution (Time::NS);

  int reconciliationMode = 0;
  int invIntervalSeconds = 1;
  int reconciliationIntervalSeconds = 30;
  int blackHoles = 0;

  int lowfanoutOrderOut = 0;
  int lowfanoutOrderInPercent = 0;

  int loopAccomodation = 0;

  double qEstimationMultiplier = 0;

  CommandLine cmd;
  cmd.AddValue ("nodes", "The total number of nodes in the network", totalNoNodes);
  cmd.AddValue ("minConnections", "The minConnectionsPerNode of the grid", minConnectionsPerNode);
  cmd.AddValue ("maxConnections", "The maxConnectionsPerNode of the grid", maxConnectionsPerNode);

  cmd.AddValue ("simulTime", "Simulation time", simulTime);
  cmd.AddValue ("publicIPNodes", "How many nodes has public IP", publicIPNodes);

  cmd.AddValue ("protocol", "Used protocol: 0 — Default, 1 — Filters on links", protocol);
  cmd.AddValue ("reconciliationMode", "reconciliation mode: 0 — Off, 1 — Time-based, 2 — Set size based", reconciliationMode);
  cmd.AddValue ("invIntervalSeconds", "invIntervalSeconds", invIntervalSeconds);
  cmd.AddValue ("reconciliationIntervalSeconds", "reconciliationIntervalSeconds", reconciliationIntervalSeconds);
  cmd.AddValue ("blackHoles", "black hole nodes", blackHoles);

  cmd.AddValue ("lowfanoutOrderOut", "lowfanout order to out connections in units", lowfanoutOrderOut);
  cmd.AddValue ("lowfanoutOrderInPercent", "lowfanout order to in connections in percent", lowfanoutOrderInPercent);
  cmd.AddValue ("loopAccomodation", "0 - no, 1 - yes", loopAccomodation);


  cmd.AddValue ("qEstimationMultiplier", "formula for estimations is in bitcoin-node.cc", qEstimationMultiplier);


  cmd.Parse(argc, argv);

  // TODO Configure
  uint averageBlockGenInterval = 10 * 60;
  uint targetNumberOfBlocks = 5000;

  stop = targetNumberOfBlocks * averageBlockGenInterval / 60; // minutes
  nodeStatistics *stats = new nodeStatistics[totalNoNodes];

  #ifdef MPI_TEST
    // Distributed simulation setup; by default use granted time window algorithm.
    if(nullmsg)
      {
        GlobalValue::Bind ("SimulatorImplementationType",
                           StringValue ("ns3::NullMessageSimulatorImpl"));
      }
    else
      {
        GlobalValue::Bind ("SimulatorImplementationType",
                           StringValue ("ns3::DistributedSimulatorImpl"));
      }

    // Enable parallel simulator with the command line arguments
    MpiInterface::Enable (&argc, &argv);
    uint32_t systemId = MpiInterface::GetSystemId ();
    uint32_t systemCount = MpiInterface::GetSize ();
  #else
    uint32_t systemId = 0;
    uint32_t systemCount = 1;
  #endif


  LogComponentEnable("BitcoinNode", LOG_LEVEL_INFO);

  BitcoinTopologyHelper bitcoinTopologyHelper (systemCount, totalNoNodes, publicIPNodes, minConnectionsPerNode,
                                               maxConnectionsPerNode, systemId);
  // Install stack on Grid
  InternetStackHelper stack;
  bitcoinTopologyHelper.InstallStack (stack);


  // Assign Addresses to Grid
  bitcoinTopologyHelper.AssignIpv4Addresses (Ipv4AddressHelperCustom ("1.0.0.0", "255.255.255.0", false));
  ipv4InterfaceContainer = bitcoinTopologyHelper.GetIpv4InterfaceContainer();
  nodesConnections = bitcoinTopologyHelper.GetNodesConnectionsIps();
  peersDownloadSpeeds = bitcoinTopologyHelper.GetPeersDownloadSpeeds();
  peersUploadSpeeds = bitcoinTopologyHelper.GetPeersUploadSpeeds();
  nodesInternetSpeeds = bitcoinTopologyHelper.GetNodesInternetSpeeds();



  ProtocolSettings protocolSettings;
  protocolSettings.protocol = ProtocolType(protocol);
  protocolSettings.invIntervalSeconds = invIntervalSeconds;
  protocolSettings.lowfanoutOrderInPercent = lowfanoutOrderInPercent;
  protocolSettings.lowfanoutOrderOut = lowfanoutOrderOut;
  protocolSettings.loopAccomodation = loopAccomodation;
  protocolSettings.reconciliationMode = reconciliationMode;
  protocolSettings.reconciliationIntervalSeconds = reconciliationIntervalSeconds;
  protocolSettings.qEstimationMultiplier = qEstimationMultiplier;


  //Install simple nodes
  BitcoinNodeHelper bitcoinNodeHelper ("ns3::TcpSocketFactory", InetSocketAddress (Ipv4Address::GetAny (), bitcoinPort),
                                        nodesConnections[0], peersDownloadSpeeds[0],  peersUploadSpeeds[0], nodesInternetSpeeds[0], stats,
                                      protocolSettings);
  ApplicationContainer bitcoinNodes;


  std::map<int, int> nodeSystemIds;

  assert(TX_EMITTERS + blackHoles <= totalNoNodes);
  for(auto &node : nodesConnections)
  {
    Ptr<Node> targetNode = bitcoinTopologyHelper.GetNode (node.first);

  	if (systemId == targetNode->GetSystemId())
  	{
      bitcoinNodeHelper.SetPeersAddresses (node.second);
      bitcoinNodeHelper.SetPeersDownloadSpeeds (peersDownloadSpeeds[node.first]);
      bitcoinNodeHelper.SetPeersUploadSpeeds (peersUploadSpeeds[node.first]);
      bitcoinNodeHelper.SetNodeInternetSpeeds (nodesInternetSpeeds[node.first]);

      auto outPeers = bitcoinTopologyHelper.GetPeersOutConnections(node.first);
      auto mode = REGULAR;
      if (node.first < TX_EMITTERS) {
        mode = TX_EMITTER;
      }
      else if (node.first < TX_EMITTERS + blackHoles) {
        mode = BLACK_HOLE;
      }

      bitcoinNodeHelper.SetProperties(simulTime, mode, systemId, outPeers);
  	  bitcoinNodeHelper.SetNodeStats (&stats[node.first]);
      bitcoinNodes.Add(bitcoinNodeHelper.Install (targetNode));

      if (systemId == 0)
        nodesInSystemId0++;
  	}
  }

  bitcoinNodes.Start (Seconds (start));
  bitcoinNodes.Stop (Minutes (stop));

  tStartSimulation = get_wall_time();


  if (systemId == 0) {
    std::cout << "start: " << start << "\n";
    std::cout << "stop: " << stop << "\n";
    std::cout << "The applications have been setup.\n";
    std::cout << "Setup time = " << tStartSimulation - tStart << "s\n";
    std::cout << "Total nodes: " << totalNoNodes << "\n";
  }
  Simulator::Stop (Minutes (stop + 0.1));

  Simulator::Run ();
  Simulator::Destroy ();

  #ifdef MPI_TEST

    int            blocklen[15] = {1, 1, 1, 1, 1, 1, 1, 1,
                                   1, 1, 1, 1, 1, 1, 1};
    MPI_Aint       disp[15];
    MPI_Datatype   dtypes[15] = {MPI_INT, MPI_LONG, MPI_LONG, MPI_LONG, MPI_LONG, MPI_LONG, MPI_INT,
                                 MPI_DOUBLE, MPI_INT, MPI_INT, MPI_INT, MPI_DOUBLE, MPI_INT, MPI_INT, MPI_INT};
    MPI_Datatype   mpi_nodeStatisticsType;

    disp[0] = offsetof(nodeStatistics, nodeId);
    disp[1] = offsetof(nodeStatistics, invReceivedMessages);
    disp[2] = offsetof(nodeStatistics, uselessInvReceivedMessages);
    disp[3] = offsetof(nodeStatistics, reconInvReceivedMessages);
    disp[4] = offsetof(nodeStatistics, reconUselessInvReceivedMessages);
    disp[5] = offsetof(nodeStatistics, txCreated);
    disp[6] = offsetof(nodeStatistics, connections);
    disp[7] = offsetof(nodeStatistics, firstSpySuccess);
    disp[8] = offsetof(nodeStatistics, txReceived);
    disp[9] = offsetof(nodeStatistics, systemId);
    disp[10] = offsetof(nodeStatistics, ignoredFilters);
    disp[11] = offsetof(nodeStatistics, reconcilDiffsAverage);
    disp[12] = offsetof(nodeStatistics, reconcilSetSizeAverage);
    disp[13] = offsetof(nodeStatistics, reconcils);
    disp[14] = offsetof(nodeStatistics, mode);


    MPI_Type_create_struct (15, blocklen, disp, dtypes, &mpi_nodeStatisticsType);
    MPI_Type_commit (&mpi_nodeStatisticsType);

    if (systemId != 0 && systemCount > 1)
    {
      for(int i = 0; i < totalNoNodes; i++)
      {
        Ptr<Node> targetNode = bitcoinTopologyHelper.GetNode (i);

    	  if (systemId == targetNode->GetSystemId())
    	  {
            MPI_Send(&stats[i], 1, mpi_nodeStatisticsType, 0, 8888, MPI_COMM_WORLD);
    	  }
      }
    }
    else if (systemId == 0 && systemCount > 1)
    {
      int count = nodesInSystemId0;

    	while (count < totalNoNodes)
    	{
    	  MPI_Status status;
        nodeStatistics recv;

    	  MPI_Recv(&recv, 1, mpi_nodeStatisticsType, MPI_ANY_SOURCE, 8888, MPI_COMM_WORLD, &status);
        stats[recv.nodeId].nodeId = recv.nodeId;
        stats[recv.nodeId].connections = recv.connections;
        stats[recv.nodeId].txCreated = recv.txCreated;
        stats[recv.nodeId].invReceivedMessages = recv.invReceivedMessages;
        stats[recv.nodeId].uselessInvReceivedMessages = recv.uselessInvReceivedMessages;
        stats[recv.nodeId].reconInvReceivedMessages = recv.reconInvReceivedMessages;
        stats[recv.nodeId].reconUselessInvReceivedMessages = recv.reconUselessInvReceivedMessages;
        stats[recv.nodeId].firstSpySuccess = recv.firstSpySuccess;
        stats[recv.nodeId].txReceived = recv.txReceived;
        stats[recv.nodeId].systemId = recv.systemId;
        stats[recv.nodeId].ignoredFilters = recv.ignoredFilters;
        stats[recv.nodeId].reconcilDiffsAverage = recv.reconcilDiffsAverage;
        stats[recv.nodeId].reconcilSetSizeAverage = recv.reconcilSetSizeAverage;
        stats[recv.nodeId].reconcils = recv.reconcils;
        stats[recv.nodeId].mode = recv.mode;
  	    count++;
      }
    }

    CollectTxData(stats, totalNoNodes, systemId, systemCount, nodesInSystemId0, bitcoinTopologyHelper);
    CollectReconcilData(stats, totalNoNodes, systemId, systemCount, nodesInSystemId0, bitcoinTopologyHelper);
  #endif


  if (systemId == 0)
  {
    tFinish=get_wall_time();

    PrintStatsForEachNode(stats, totalNoNodes, publicIPNodes, blackHoles);


    std::cout << "\nThe simulation ran for " << tFinish - tStart << "s simulating "
              << stop << "mins. Performed " << stop * secsPerMin / (tFinish - tStart)
              << " faster than realtime.\n" << "Setup time = " << tStartSimulation - tStart << "s\n"
              <<"It consisted of " << totalNoNodes << " nodes ( with minConnectionsPerNode = "
              << minConnectionsPerNode << " and maxConnectionsPerNode = " << maxConnectionsPerNode
              << "\n" << "Protocol Type: " << protocol << "\n";

  }

  #ifdef MPI_TEST

    // Exit the MPI execution environment
    MpiInterface::Disable ();


  #else
     NS_FATAL_ERROR ("Can't use distributed simulator without MPI compiled in");
   #endif

  delete[] stats;

  return 0;
//
// #else
//   NS_FATAL_ERROR ("Can't use distributed simulator without MPI compiled in");
// #endif
}

double get_wall_time()
{
    struct timeval time;
    if (gettimeofday(&time,NULL)){
        //  Handle error
        return 0;
    }
    return (double)time.tv_sec + (double)time.tv_usec * .000001;
}

int GetNodeIdByIpv4 (Ipv4InterfaceContainer container, Ipv4Address addr)
{
  for (auto it = container.Begin(); it != container.End(); it++)
  {
	int32_t interface = it->first->GetInterfaceForAddress (addr);
	if ( interface != -1)
      return it->first->GetNetDevice (interface)-> GetNode()->GetId();
  }
  return -1; //if not found
}

void PrintStatsForEachNode (nodeStatistics *stats, int totalNodes, int publicIPNodes, int blackHoles)
{
  std::map<int, std::vector<double>> allTxRelayTimes;

  int ignoredFilters = 0;

  double publicNodesAverageReconcilDiff;
  double publicNodesAverageReconcilSetSize;

  double privateNodesAverageReconcilDiff;
  double privateNodesAverageReconcilSetSize;

  int reconcilDiffsDistr[DIFFS_DISTR_SIZE]{0};


  long invReceivedTotal = 0;
  long uselessInvReceivedTotal = 0;

  long reconInvReceivedTotal = 0;
  long reconUselessInvReceivedTotal = 0;

  long totalSyndromesSent = 0; // excluding failed
  long extraSyndromesSent = 0; // overestimation
  long totalReconciliationsFailed = 0;
  long totalReconciliations = 0;


  for (int it = 0; it < totalNodes; it++ )
  {
    if (stats[it].mode == BLACK_HOLE)
      continue;

    // std::cout << "\nNode " << stats[it].nodeId << " statistics:\n";
    // std::cout << "Connections = " << stats[it].connections << "\n";
    // std::cout << "Transactions created = " << stats[it].txCreated << "\n";
    // std::cout << "Inv received = " << stats[it].invReceivedMessages << "\n";
    // std::cout << "Tx received = " << stats[it].txReceived << "\n";
    //
    for (auto el: stats[it].reconcilData) {
      if (el.diffSize < DIFFS_DISTR_SIZE - 1)
        reconcilDiffsDistr[el.diffSize]++;
      else
        reconcilDiffsDistr[DIFFS_DISTR_SIZE - 1]++;
      if (el.estimatedDiff < el.diffSize) {
        totalReconciliationsFailed++;
      } else {
        totalSyndromesSent += el.estimatedDiff;
        extraSyndromesSent += (el.estimatedDiff - el.diffSize);
      }
      totalReconciliations++;
    }
    invReceivedTotal += stats[it].invReceivedMessages;
    uselessInvReceivedTotal += stats[it].uselessInvReceivedMessages;

    reconInvReceivedTotal += stats[it].reconInvReceivedMessages;
    reconUselessInvReceivedTotal += stats[it].reconUselessInvReceivedMessages;


    for (int txCount = 0; txCount < stats[it].txReceived; txCount++)
    {
      txRecvTime txTime = stats[it].txReceivedTimes[txCount];
      allTxRelayTimes[txTime.txHash].push_back(txTime.txTime);
    }
  }
  int activeNodes = totalNodes - blackHoles;


  std::cout << "INVs sent in the network: " << invReceivedTotal << std::endl;
  std::cout << "Useless % INVs in the network: " << uselessInvReceivedTotal * 1.0 / invReceivedTotal << std::endl;

  std::cout << "Recon INVs sent in the network: " << reconInvReceivedTotal << std::endl;
  std::cout << "Recon Useless % INVs in the network: " << reconUselessInvReceivedTotal * 1.0 / reconInvReceivedTotal << std::endl;

  std::cout << "Total syndromes sent: " << totalSyndromesSent << std::endl;
  std::cout << "Extra syndromes sent (overestimation): " << extraSyndromesSent << std::endl;
  std::cout << "Reconciliations: " << totalReconciliations << std::endl;
  std::cout << "Reconciliations failed: " << totalReconciliationsFailed << std::endl;


  std::cout << "Public nodes Average reconciliation diff = " << publicNodesAverageReconcilDiff / (publicIPNodes - blackHoles) << "\n";
  std::cout << "Public nodes Average reconciliation set size = " << publicNodesAverageReconcilSetSize / (publicIPNodes - blackHoles) << "\n";

  std::cout << "Private nodes Average reconciliation diff = " << privateNodesAverageReconcilDiff / (totalNodes - publicIPNodes) << "\n";
  std::cout << "Private nodes Average reconciliation set size = " << privateNodesAverageReconcilSetSize / (totalNodes - publicIPNodes) << "\n";


  for (int i = 0; i < DIFFS_DISTR_SIZE; i++) {
    std::cout << reconcilDiffsDistr[i] << ", ";
  }
  std::cout << std::endl;

  // std::vector<double> fiftyPercentRelayTimes;
  // std::vector<double> seventyFivePercentRelayTimes;
  // std::vector<double> ninetyNinePercentRelayTimes;
  // std::vector<double> fullRelayTimes;

  const int GRANULARITY = 20;
  std::vector<std::vector<double>> percentRelayTimes(GRANULARITY);


  for (std::map<int, std::vector<double>>::iterator txTimes=allTxRelayTimes.begin();
    txTimes!=allTxRelayTimes.end(); ++txTimes)
  {
    std::vector<double> relayTimes = txTimes->second;
    std::sort(relayTimes.begin(), relayTimes.end());
    int i = 0;
    while (i < GRANULARITY)
    {
      double currentFraction = (i+1) * 1.0 / GRANULARITY - 0.01;
      if (relayTimes.size() <= activeNodes * currentFraction) {
        break;
      }
      percentRelayTimes[i].push_back(relayTimes.at(int(relayTimes.size() * currentFraction)) - relayTimes.front());
      i++;
    }
  }

  for (int i = 0; i < GRANULARITY; i++) {
    std::cout <<  (i + 1) * (100 / GRANULARITY) - 1 << "% to ";
    std::cout << " relay time: " << accumulate(percentRelayTimes[i].begin(), percentRelayTimes[i].end(), 0.0) / percentRelayTimes[i].size() << ", txs: " << percentRelayTimes[i].size() << "\n";
  }


}

int PoissonDistribution(int value) {
    // const uint64_t range_from  = 0;
    // const uint64_t range_to    = 1ULL << 48;
    // std::random_device                  rand_dev;
    // std::mt19937                        generator(rand_dev());
    // std::uniform_int_distribution<uint64_t>  distr(range_from, range_to);
    // auto bigRand = distr(generator);
    std::default_random_engine generator;
    std::uniform_int_distribution<int> distribution(0, INT_MAX);
    int bigRand = distribution(generator);

    return (int)(log1p(bigRand * -0.0000000000000035527136788 /* -1/2^48 */) * value * -1 + 0.5);
}

std::vector<int> generateTxCreateList(int n, int nodes) {
  std::vector<int> result;
  int averageTxPerNode = std::max(n / nodes, 1);
  int alreadyAssigned = 0;
  for (int i = 0; i < nodes - 1; i++) {
    int txToCreate = PoissonDistribution(averageTxPerNode);
    result.push_back(txToCreate);
    alreadyAssigned += txToCreate;
    if (alreadyAssigned > n) {
      result[i] -= (n - alreadyAssigned);
      for (int j = i; j < nodes; j++)
        result.push_back(0);
      break;
    }
  }
  result.push_back(n - alreadyAssigned);
  return result;
}


void CollectTxData(nodeStatistics *stats, int totalNoNodes,
  int systemId, int systemCount, int nodesInSystemId0, BitcoinTopologyHelper bitcoinTopologyHelper)
{
#ifdef MPI_TEST
  int            blocklen[3] = {1, 1, 1};
  MPI_Aint       disp[3];
  MPI_Datatype   dtypes[3] = {MPI_INT, MPI_INT, MPI_INT};
  MPI_Datatype   mpi_txRecvTime;

  disp[0] = offsetof(txRecvTime, nodeId);
  disp[1] = offsetof(txRecvTime, txHash);
  disp[2] = offsetof(txRecvTime, txTime);

  MPI_Type_create_struct (3, blocklen, disp, dtypes, &mpi_txRecvTime);
  MPI_Type_commit (&mpi_txRecvTime);

  if (systemId != 0 && systemCount > 1)
  {
    for(int i = nodesInSystemId0; i < totalNoNodes; i++)
    {
      Ptr<Node> targetNode = bitcoinTopologyHelper.GetNode(i);
      if (stats[i].mode == BLACK_HOLE)
        continue;
      if (systemId == targetNode->GetSystemId())
      {
          for (int j = 0; j < stats[i].txReceived; j++) {
            MPI_Send(&stats[i].txReceivedTimes[j], 1, mpi_txRecvTime, 0, 9999, MPI_COMM_WORLD);
          }
      }
    }
  }
  else if (systemId == 0 && systemCount > 1)
  {
    int count = 0;
    MPI_Status status;
    txRecvTime recv;

    while (count < totalNoNodes)
    {
      if (stats[count].systemId == 0 || stats[count].mode == BLACK_HOLE) {
        count++;
        continue;
      }
      for (int j = 0; j < stats[count].txReceived; j++)
       {
          MPI_Recv(&recv, 1, mpi_txRecvTime, MPI_ANY_SOURCE, 9999, MPI_COMM_WORLD, &status);
          stats[recv.nodeId].txReceivedTimes.push_back(recv);
      }
      count++;
    }
  }
#endif
}


void CollectReconcilData(nodeStatistics *stats, int totalNoNodes,
  int systemId, int systemCount, int nodesInSystemId0, BitcoinTopologyHelper bitcoinTopologyHelper)
{
  // return;
  #ifdef MPI_TEST
    int            blocklen[5] = {1, 1, 1, 1, 1};
    MPI_Aint       disp[5];
    MPI_Datatype   dtypes[5] = {MPI_INT, MPI_INT, MPI_INT, MPI_INT, MPI_INT};
    MPI_Datatype   mpi_reconcilItem;

    disp[0] = offsetof(reconcilItem, nodeId);
    disp[1] = offsetof(reconcilItem, setInSize);
    disp[2] = offsetof(reconcilItem, setOutSize);
    disp[3] = offsetof(reconcilItem, diffSize);
    disp[4] = offsetof(reconcilItem, estimatedDiff);

    MPI_Type_create_struct (5, blocklen, disp, dtypes, &mpi_reconcilItem);
    MPI_Type_commit (&mpi_reconcilItem);

    if (systemId != 0 && systemCount > 1)
    {
      for(int i = nodesInSystemId0; i < totalNoNodes; i++)
      {
        Ptr<Node> targetNode = bitcoinTopologyHelper.GetNode(i);
        if (systemId == targetNode->GetSystemId())
        {
            if (stats[i].mode == BLACK_HOLE)
              continue;
            for (int j = 0; j < stats[i].reconcils; j++) {
              MPI_Send(&stats[i].reconcilData[j], 1, mpi_reconcilItem, 0, 6666, MPI_COMM_WORLD);
            }
        }
      }
    }
    else if (systemId == 0 && systemCount > 1)
    {
      int count = 0;
      MPI_Status status;
      reconcilItem recv;

      while (count < totalNoNodes)
      {
        if (stats[count].systemId == 0) {
          count++;
          continue;
        }
        if (stats[count].mode == BLACK_HOLE) {
          count++;
          continue;
        }
        for (int j = 0; j < stats[count].reconcils; j++)
         {
            MPI_Recv(&recv, 1, mpi_reconcilItem, MPI_ANY_SOURCE, 6666, MPI_COMM_WORLD, &status);
            stats[recv.nodeId].reconcilData.push_back(recv);
        }
        count++;
      }
    }
  #endif
}
