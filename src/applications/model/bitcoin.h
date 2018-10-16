/**
 * This file contains all the necessary enumerations and structs used throughout the simulation.
 * It also defines 3 very important classed; the Block, Chunk and Blockchain.
 */


#ifndef BITCOIN_H
#define BITCOIN_H

#include <vector>
#include <map>
#include "ns3/address.h"
#include <algorithm>

namespace ns3 {

const int DIFFS_DISTR_SIZE = 350;
const int SETS_DISTR_SIZE = 4000;



const int TX_EMITTERS = 200;

const int RECON_MAX_SET_SIZE = 1600;

const int DANDELION_ROTATION_SECONDS = 1000;

/**
 * The bitcoin message types that have been implemented.
 */
enum Messages
{
  INV,              //0
  GET_DATA,         //1
  TX,
  FILTER_REQUEST,
  MODE,
  UPDATE_FILTER_BEGIN,
  UPDATE_FILTER_END,
  RECONCILE_TX_REQUEST,
  RECONCILE_TX_RESPONSE
};

enum ProtocolType
{
  STANDARD_PROTOCOL,           //DEFAULT
  FILTERS_ON_INCOMING_LINKS,
  OUTGOING_FILTERS,
  PREFERRED_OUT_DESTINATIONS,
  PREFERRED_ALL_DESTINATIONS,
  DANDELION_MAPPING,
};

enum ReconcilStrategy
{
  RECON_OFF,           //DEFAULT
  TIME_BASED,
  SET_SIZE_BASED,
};


enum ModeType
{
  REGULAR,           //DEFAULT
  TX_EMITTER,
  BLACK_HOLE,
  SPY
};

typedef struct {
  int nodeId;
  int txHash;
  double txTime;
} txRecvTime;

typedef struct {
  int nodeId;
  int setInSize;
  int setOutSize;
  int diffSize;
  int estimatedDiff;
} reconcilItem;



/**
 * The struct used for collecting node statistics.
 */
typedef struct {
  int      nodeId;
  long     invReceivedMessages;
  long     uselessInvReceivedMessages;
  long     reconInvReceivedMessages;
  long     reconUselessInvReceivedMessages;
  long     txCreated;
  int      connections;
  double firstSpySuccess;
  long onTheFlyCollisions;

  int txReceived;
  int systemId;

  std::vector<txRecvTime> txReceivedTimes;
  int ignoredFilters;

  double reconcilDiffsAverage;
  int reconcilSetSizeAverage;

  int reconcils;
  std::vector<reconcilItem> reconcilData;

  int mode;
} nodeStatistics;

typedef struct {
  long numUsefulInvReceived;
  long numUselessInvReceived;
  long numGetDataReceived;
  long numGetDataSent;
  double connectionLength;
  double usefulInvRate;
} peerStatistics;

typedef struct {
  double downloadSpeed;
  double uploadSpeed;
} nodeInternetSpeeds;

typedef struct {
  ProtocolType protocol;
  int invIntervalSeconds;

  int loopAccommodation;
  int lowfanoutOrderInPercent;
  int lowfanoutOrderOut;


  int reconciliationMode;
  int reconciliationIntervalSeconds;
  double qEstimationMultiplier;
} ProtocolSettings;

#define FILTER_BASE_NUMBERING 1000

}// Namespace ns3

#endif /* BITCOIN_H */
