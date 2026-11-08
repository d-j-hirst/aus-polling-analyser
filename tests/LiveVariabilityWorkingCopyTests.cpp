// Verify the mutable-state boundary for reused uncertainty-preparation copies.
// Fictional booths keep accepted counts separate from sample scratch values.
#include "../LiveVariability.h"
#include "../LivePreparedStorage.h"

#include <cassert>
#include <iostream>

namespace {
struct Node {
    LiveVariability::Counts tempFpVotesProjected, tempTcpVotesProjected, tempTppVotesProjected;
    double tcpCompletion=0, tcpConfidence=0, tppCompletion=0, tppConfidence=0;
    std::map<int,int> fpVotesCurrent, tcpVotesCurrent;
    LiveVariability::Counts fpVotesProjected;
    bool operator==(Node const&) const = default;
};
struct Booth {
    Node node;
    std::map<int,double> futureFpShares;
    std::string name;
    bool operator==(Booth const&) const = default;
};

void restoresPreparedContentsAndLeavesInputsAlone() {
    Booth prepared;
    prepared.name="Fictional partial count";
    prepared.node.fpVotesCurrent={{0,300},{1,250},{2,150}};
    prepared.node.tcpVotesCurrent={{0,360},{1,340}};
    prepared.node.fpVotesProjected={{0,400.},{1,300.},{2,200.}};
    // Non-empty scratch contents exercise restoration rather than assuming
    // every preparation starts with empty maps and zero confidence.
    prepared.node.tempFpVotesProjected={{0,1.},{1,2.}};
    prepared.node.tempTcpVotesProjected={{3,3.},{4,4.}};
    prepared.node.tempTppVotesProjected={{0,5.},{1,6.}};
    prepared.futureFpShares={{0,.25},{1,.5},{2,.25}};
    prepared.node.tcpCompletion=.71; prepared.node.tcpConfidence=.23;
    prepared.node.tppCompletion=.61; prepared.node.tppConfidence=.43;
    auto original=prepared;
    auto working=prepared;
    for(int sample=0;sample<288;++sample) {
        working.node.tempFpVotesProjected={{-2,double(sample)}};
        working.node.tempTcpVotesProjected.clear();
        working.node.tempTppVotesProjected={{5,300.}};
        working.futureFpShares={{7,1.}};
        working.node.tcpCompletion=working.node.tcpConfidence=1;
        working.node.tppCompletion=working.node.tppConfidence=0;
        LiveVariability::restoreBoothForSample(working,prepared);
        assert(working==prepared);
        assert(prepared==original);
    }
}

void preparationCopiesRemainPrivate() {
    LivePreparedStorage<Booth> prepared;
    prepared.edit().resize(2);
    prepared.edit()[0].futureFpShares={{0,.4},{1,.6}};
    prepared.edit()[1].node.fpVotesCurrent={{0,100},{1,200}};
    auto first=prepared,second=prepared;
    first.edit()[0].futureFpShares={{1,1.}};
    second.edit()[1].node.tempTcpVotesProjected={{3,999.}};
    LiveVariability::restoreBoothForSample(first.edit()[0],prepared.read()[0]);
    assert(first.read()[0]==prepared.read()[0]);
    assert(second.read()[1].node.tempTcpVotesProjected.at(3)==999);
    assert(prepared.read()[1].node.tempTcpVotesProjected.empty());
    assert((prepared.read()[1].node.fpVotesCurrent==std::map<int,int>({{0,100},{1,200}})));
}
}

int main() {
    restoresPreparedContentsAndLeavesInputsAlone();
    preparationCopiesRemainPrivate();
    std::cout<<"Preparation scratch restoration and private-copy checks passed\n";
}
