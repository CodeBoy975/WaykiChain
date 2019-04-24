// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2019- The WaykiChain Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php

#include "accounts.h"
#include "database.h"
#include "main.h"

// /** account db cache*/
// extern CAccountViewCache *pAccountViewTip;

string CAccountLog::ToString() const {
    string str("");
    str += strprintf("    Account log: keyId=%d bcoinBalance=%lld nVoteHeight=%lld receivedVotes=%lld \n",
        keyID.GetHex(), bcoinBalance, nVoteHeight, receivedVotes);
    str += string("    vote fund:");

    for (auto it =  vVoteFunds.begin(); it != vVoteFunds.end(); ++it) {
        str += it->ToString();
    }
    return str;
}

bool CAccount::UndoOperateAccount(const CAccountLog & accountLog) {
    LogPrint("undo_account", "after operate:%s\n", ToString());
    bcoinBalance    = accountLog.bcoinBalance;
    nVoteHeight = accountLog.nVoteHeight;
    vVoteFunds  = accountLog.vVoteFunds;
    receivedVotes     = accountLog.receivedVotes;
    LogPrint("undo_account", "before operate:%s\n", ToString().c_str());
    return true;
}

uint64_t CAccount::GetAccountProfit(uint64_t nCurHeight) {
     if (vVoteFunds.empty()) {
        LogPrint("DEBUG", "1st-time vote for the account, hence no minting of interest.");
        nVoteHeight = nCurHeight; //record the 1st-time vote block height into account
        return 0; // 0 profit for 1st-time vote
    }

    // 先判断计算分红的上下限区块高度是否落在同一个分红率区间
    uint64_t nBeginHeight = nVoteHeight;
    uint64_t nEndHeight = nCurHeight;
    uint64_t nBeginSubsidy = IniCfg().GetBlockSubsidyCfg(nVoteHeight);
    uint64_t nEndSubsidy = IniCfg().GetBlockSubsidyCfg(nCurHeight);
    uint64_t nValue = vVoteFunds.begin()->GetVoteCount();
    LogPrint("profits", "nBeginSubsidy:%lld nEndSubsidy:%lld nBeginHeight:%d nEndHeight:%d\n",
        nBeginSubsidy, nEndSubsidy, nBeginHeight, nEndHeight);

    // 计算分红
    auto calculateProfit = [](uint64_t nValue, uint64_t nSubsidy, int nBeginHeight, int nEndHeight) -> uint64_t {
        int64_t nHoldHeight = nEndHeight - nBeginHeight;
        int64_t nYearHeight = SysCfg().GetSubsidyHalvingInterval();
        uint64_t llProfits =  (uint64_t)(nValue * ((long double)nHoldHeight * nSubsidy / nYearHeight / 100));
        LogPrint("profits", "nValue:%lld nSubsidy:%lld nBeginHeight:%d nEndHeight:%d llProfits:%lld\n",
            nValue, nSubsidy, nBeginHeight, nEndHeight, llProfits);
        return llProfits;
    };

    // 如果属于同一个分红率区间，分红=区块高度差（持有高度）* 分红率；如果不属于同一个分红率区间，则需要根据分段函数累加每一段的分红
    uint64_t llProfits = 0;
    uint64_t nSubsidy = nBeginSubsidy;
    while (nSubsidy != nEndSubsidy) {
        int nJumpHeight = IniCfg().GetBlockSubsidyJumpHeight(nSubsidy - 1);
        llProfits += calculateProfit(nValue, nSubsidy, nBeginHeight, nJumpHeight);
        nBeginHeight = nJumpHeight;
        nSubsidy -= 1;
    }

    llProfits += calculateProfit(nValue, nSubsidy, nBeginHeight, nEndHeight);
    LogPrint("profits", "updateHeight:%d curHeight:%d freeze value:%lld\n",
        nVoteHeight, nCurHeight, vVoteFunds.begin()->GetVoteCount());

    nVoteHeight = nCurHeight;
    return llProfits;
}

uint64_t CAccount::GetRawBalance() {
    return bcoinBalance;
}

uint64_t CAccount::GetFrozenBalance() {
    uint64_t votes = 0;
    if (!vVoteFunds.empty()) {
        for (auto it = vVoteFunds.begin(); it != vVoteFunds.end(); it++) {
            votes += it->GetVoteCount(); //one coin one vote!
        }
    }
    return votes;
}

uint64_t CAccount::GetTotalBalance() {
    uint64_t frozenVotes = GetFrozenBalance();
    return ( frozenVotes + bcoinBalance );
}

Object CAccount::ToJsonObj(bool isAddress) const {
    Array voteFundArray;
    for (auto &fund : vVoteFunds) {
        voteFundArray.push_back(fund.ToJson());
    }

    Object obj;
    bool isMature = (regID.GetHeight() == 0 ||
                     chainActive.Height() - (int)regID.GetHeight() > kRegIdMaturePeriodByBlock)
                        ? true
                        : false;
    obj.push_back(Pair("address", keyID.ToAddress()));
    obj.push_back(Pair("key_id", keyID.ToString()));
    obj.push_back(Pair("public_key", pubKey.ToString()));
    obj.push_back(Pair("miner_public_key", minerPubKey.ToString()));
    obj.push_back(Pair("reg_id", regID.ToString()));
    obj.push_back(Pair("reg_id_mature", isMature));
    obj.push_back(Pair("balance", bcoinBalance));
    obj.push_back(Pair("update_height", nVoteHeight));
    obj.push_back(Pair("votes", receivedVotes));
    obj.push_back(Pair("vote_fund_list", voteFundArray));
    return obj;
}

string CAccount::ToString(bool isAddress) const {
    string str;
    str += strprintf("regID=%s, keyID=%s, publicKey=%s, minerpubkey=%s, values=%ld updateHeight=%d receivedVotes=%lld\n",
        regID.ToString(), keyID.GetHex().c_str(), pubKey.ToString().c_str(),
        minerPubKey.ToString().c_str(), bcoinBalance, nVoteHeight, receivedVotes);
    str += "vVoteFunds list: \n";
    for (auto & fund : vVoteFunds) {
        str += fund.ToString();
    }
    return str;
}

bool CAccount::IsMoneyOverflow(uint64_t nAddMoney) {
    if (!CheckMoneyRange(nAddMoney))
        return ERRORMSG("money:%lld too larger than MaxMoney");

    return true;
}

bool CAccount::OperateAccount(OperType type, const uint64_t &value, const uint64_t nCurHeight) {
    LogPrint("op_account", "before operate:%s\n", ToString());
    if (!IsMoneyOverflow(value))
        return false;
    if (keyID == uint160()) {
        return ERRORMSG("operate account's keyId is 0 error");
    }
    if (!value)
        return true;
    switch (type) {
    case ADD_FREE: {
        bcoinBalance += value;
        if (!IsMoneyOverflow(bcoinBalance))
            return false;
        break;
    }
    case MINUS_FREE: {
        if (value > bcoinBalance)
            return false;
        bcoinBalance -= value;
        break;
    }
    default:
        return ERRORMSG("operate account type error!");
    }
    LogPrint("op_account", "after operate:%s\n", ToString());
    return true;
}

bool CAccount::ProcessDelegateVote(vector<COperVoteFund> & operVoteFunds, const uint64_t nCurHeight) {
    if (nCurHeight < nVoteHeight) {
        LogPrint("ERROR", "current vote tx height (%d) can't be smaller than the last nVoteHeight (%d)",
            nCurHeight, nVoteHeight);
        return false;
    }

    uint64_t llProfit = GetAccountProfit(nCurHeight);
    if (!IsMoneyOverflow(llProfit)) return false;

    uint64_t totalVotes = vVoteFunds.empty() ? 0 : vVoteFunds.begin()->GetVoteCount();

    for (auto operVote = operVoteFunds.begin(); operVote != operVoteFunds.end(); ++operVote) {
        const CUserID &voteId = operVote->fund.GetVoteId();
        vector<CVoteFund>::iterator itfund =
            find_if(vVoteFunds.begin(), vVoteFunds.end(),
                    [voteId](CVoteFund fund) { return fund.GetVoteId() == voteId; });

        int voteType = VoteOperType(operVote->operType);
        if (ADD_FUND == voteType) {
            if (itfund != vVoteFunds.end()) {
                uint64_t currVotes = itfund->GetVoteCount();

                if (!IsMoneyOverflow(operVote->fund.GetVoteCount()))
                     return ERRORMSG("ProcessDelegateVote() : oper fund value exceed maximum ");

                itfund->SetVoteCount( currVotes + operVote->fund.GetVoteCount() );

                if (!IsMoneyOverflow(itfund->GetVoteCount()))
                     return ERRORMSG("ProcessDelegateVote() : fund value exceeds maximum");

            } else {
               vVoteFunds.push_back(operVote->fund);
               if (vVoteFunds.size() > IniCfg().GetDelegatesNum()) {
                   return ERRORMSG("ProcessDelegateVote() : fund number exceeds maximum");
               }
            }
        } else if (MINUS_FUND == voteType) {
            if  (itfund != vVoteFunds.end()) {
                uint64_t currVotes = itfund->GetVoteCount();

                if (!IsMoneyOverflow(operVote->fund.GetVoteCount()))
                    return ERRORMSG("ProcessDelegateVote() : oper fund value exceed maximum ");

                if (itfund->GetVoteCount() < operVote->fund.GetVoteCount())
                    return ERRORMSG("ProcessDelegateVote() : oper fund value exceed delegate fund value");

                itfund->SetVoteCount( currVotes - operVote->fund.GetVoteCount() );

                if (0 == itfund->GetVoteCount())
                    vVoteFunds.erase(itfund);

            } else {
                return ERRORMSG("ProcessDelegateVote() : revocation votes not exist");
            }
        } else {
            return ERRORMSG("ProcessDelegateVote() : operType: %d invalid", voteType);
        }
    }

    // sort account votes after the operations against the new votes
    std::sort(vVoteFunds.begin(), vVoteFunds.end(), [](CVoteFund fund1, CVoteFund fund2) {
        return fund1.GetVoteCount() > fund2.GetVoteCount();
    });

    // get the maximum one as the vote amount
    uint64_t newTotalVotes = vVoteFunds.empty() ? 0 : vVoteFunds.begin()->GetVoteCount();

    if (bcoinBalance + totalVotes < newTotalVotes) {
        return  ERRORMSG("ProcessDelegateVote() : delegate value exceed account value");
    }
    bcoinBalance = (bcoinBalance + totalVotes) - newTotalVotes;
    bcoinBalance += llProfit;
    LogPrint("profits", "received profits: %lld\n", llProfit);
    return true;
}

bool CAccount::OperateVote(VoteOperType type, const uint64_t & values) {
    if(ADD_FUND == type) {
        receivedVotes += values;
        if(!IsMoneyOverflow(receivedVotes)) {
            return ERRORMSG("OperateVote() : delegates total votes exceed maximum ");
        }
    } else if (MINUS_FUND == type) {
        if(receivedVotes < values) {
            return ERRORMSG("OperateVote() : delegates total votes less than revocation vote value");
        }
        receivedVotes -= values;
    } else {
        return ERRORMSG("OperateVote() : CDelegateTx ExecuteTx AccountVoteOper revocation votes are not exist");
    }
    return true;
}
