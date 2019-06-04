// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2017-2019 The WaykiChain Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "cdpdb.h"
#include "dbconf.h"

bool CCdpCache::SetStakeBcoins(CUserID txUid, uint64_t bcoinsToStake, uint64_t collateralRatio,
                                int blockHeight, CDbOpLog &cdpDbOpLog) {

    string key = dbk::GenDbKey(dbk::CDP, txUid);
    CUserCdp lastCdp;
    if (mapCdps.count(txUid.ToString())) {
        if (!GetData(key, lastCdp)) {
            return ERRORMSG("CCdpCache::SetStakeBcoins : GetData failed.");
        }

    }

    CUserCdp cdp        = lastCdp;
    cdp.lastBlockHeight = cdp.blockHeight;
    cdp.lastOwedScoins += cdp.totalOwedScoins;
    cdp.blockHeight     = blockHeight;
    cdp.collateralRatio = collateralRatio;
    cdp.mintedScoins    = bcoinsToStake / collateralRatio;
    cdp.totalOwedScoins += cdp.mintedScoins;

    if (!SetData(key, cdp)) {
        return ERRORMSG("CCdpCache::SetStakeBcoins : SetData failed.");
    }
    cdpDbOpLog.push_back(lastCdp);

    return true;
}

bool CCdpCache::GetUnderLiquidityCdps(vector<CUserCdp> & userCdps) {
    //TODO
    return true;
}

bool CCdpCache::GetData(const string &key, CUserCdp &value) {
    if (mapCdps.count(key) > 0) {
        if (!mapCdps[key].IsEmpty()) {
            value = mapCdps[key];
            return true;
        } else {
            return false;
        }
    }

    if (!pBase->GetData(key, value)) {
        return false;
    }

    mapCdps[key] = value;  //cache it here for speed in-mem access
    return true;
}

bool CCdpCache::SetData(const string &key, const CUserCdp &value) {
    pBase->SetData(dbk::GetDbKey(dbk::CDP, key), value);
    return true;
}

bool CCdpCache::BatchWrite(const map<string, CUserCdp> &mapContractDb) {
    //TODO
    return true;
}

bool CCdpCache::EraseKey(const string &vKey) {
    //TODO
    return true;
}

bool CCdpCache::HaveData(const string &vKey) {
    //TODO
    return true;
}


CCdpDb::CCdpDb(const string &name, size_t nCacheSize, bool fMemory, bool fWipe)
    : db(GetDataDir() / "blocks" / name, nCacheSize, fMemory, fWipe) {}

CCdpDb::CCdpDb(size_t nCacheSize, bool fMemory, bool fWipe)
    : db(GetDataDir() / "blocks" / "cdp", nCacheSize, fMemory, fWipe) {}

 virtual bool GetData(const string &key, CUserCdp &value);
    virtual bool SetData(const string &key, const CUserCdp &value);
    virtual bool BatchWrite(const map<string, CUserCdp > &mapContractDb);
    virtual bool EraseKey(const string &key);
    virtual bool HaveData(const string &key);