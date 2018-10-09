# Intro

Our simulation was done via ns3. We forked open-source Bitcoin Simulator to support transaction relay (it was used to measure block relay and focused on mining). We emulated a network, similar to the current Bitcoin network.
We have 2 phases in our simulation:
  - Bootstrapping the network
  - Transaction generation and relay
Our simulation is based on:
  - INV-GETDATA transaction relay protocol
  - Current ratio of public-IP nodes to private-IP nodes in the Bitcoin network
  - Different ratios of faults in the network is simulated by introducing Black Hole nodes (nodes which receive messages, but do not propagate transactions further)

Although replication of the Bitcoin network topology would the most fair experiments, it has been shown that inferring the Bitcoin network topology is not trivial.

The topology of simulated network is random, but at the same time similar to the actual Bitcoin network because of the same 2 phases of bootstrapping:
  - Public-IP nodes connect to each other having a limit of 8 outgoing connections
  - Private-IP nodes connect to 8 existing public-IP nodes

Our simulation does not take into account:
  - Resource-wise heterogeneous setting (see Section 2.4)
  - Block relay phase
  - Joining and leaving nodes during the transaction relay phase
  - Sophisticated malicious nodes



# How to run this version

./waf --run "default-test --nodes=10000 --publicIPNodes=1000 --minConnections=8 --maxConnections=125 --simulTime=330 --protocol=4 --reconciliationMode=1 --invIntervalSeconds=3 --blackHoles=0 --lowfanoutOrderInPercent=5 --lowfanoutOrderOut=2 --loopAccomodation=0 --reconciliationIntervalSeconds=5"

lowfanoutOrderIn: in percent of incoming peers
lowfanoutOrderOut: in outgoing peers

For multi-core prepend with
```mpirun -n 8```


For installation see next paragraph

# Bitcoin-Simulator, capable of simulating any re-parametrization of Bitcoin
Bitcoin Simulator is built on ns3, the popular discrete-event simulator. We also made use of rapidjson to facilitate the communication process among the nodes. The purpose of this project is to study how consensus parameteres, network characteristics and protocol modifications affect the scalability, security and efficiency of Proof of Work powered blockchains.

Our goal is to make the simulator as realistic as possible. So, we collected real network statistics and incorporated them in the simulator. Specifically, we crawled popular explorers, like blockchain.info to estimate the block generation and block size distribution, and used the bitcoin crawler to find out the average number of nodes in the network and their geographic distribution. Futhermore, we used the data provided by coinscope regarding the connectivity of nodes.

We provide you with a detailed [installation guide](http://arthurgervais.github.io/Bitcoin-Simulator/Installation.html), containing a [video tutorial](http://arthurgervais.github.io/Bitcoin-Simulator/Installation.html), to help you get started. You can also check our [experimental results](http://arthurgervais.github.io/Bitcoin-Simulator/results.html) and our code. Feel free to contact us or any questions you may have.

# Crafted through Research

The Bitcoin-Simulator was developed as part of the following publication in CCS'16:

[On the Security and Performance of Proof of Work Blockchains](https://eprint.iacr.org/2016/555.pdf)

```latex
@inproceedings{gervais2016security,
  title={On the Security and Performance of Proof of Work Blockchains},
  author={Gervais, Arthur and Karame, Ghassan and Wüst, Karl and Glykantzis, Vasileios and Ritzdorf, Hubert and Capkun, Srdjan},
  booktitle={Proceedings of the 23nd ACM SIGSAC Conference on Computer and Communication Security (CCS)},
  year={2016},
  organization={ACM}
}
```
