// Copyright 2019 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "transactions/simulation/SimulationTransactionFrame.h"
#include "ledger/LedgerTxn.h"
#include "transactions/OperationFrame.h"
#include "transactions/TransactionUtils.h"
#include "transactions/simulation/SimulationCreatePassiveSellOfferOpFrame.h"
#include "transactions/simulation/SimulationManageBuyOfferOpFrame.h"
#include "transactions/simulation/SimulationManageSellOfferOpFrame.h"
#include "transactions/simulation/SimulationMergeOpFrame.h"

namespace stellar
{

TransactionFramePtr
SimulationTransactionFrame::makeTransactionFromWire(
    Hash const& networkID, TransactionEnvelope const& envelope,
    TransactionResult simulationResult, uint32_t count)
{
    TransactionFramePtr res = std::make_shared<SimulationTransactionFrame>(
        networkID, envelope, simulationResult, count);
    return res;
}

SimulationTransactionFrame::SimulationTransactionFrame(
    Hash const& networkID, TransactionEnvelope const& envelope,
    TransactionResult simulationResult, uint32_t count)
    : TransactionFrame(networkID, envelope)
    , mSimulationResult(simulationResult)
    , mCount(count)
{
}

std::shared_ptr<OperationFrame>
SimulationTransactionFrame::makeOperation(Operation const& op,
                                          OperationResult& res, size_t index)
{
    assert(index < mEnvelope.v0().tx.operations.size());
    OperationResult resultFromArchive;
    if (mSimulationResult.result.code() == txSUCCESS ||
        mSimulationResult.result.code() == txFAILED)
    {
        resultFromArchive = mSimulationResult.result.results()[index];
    }

    switch (mEnvelope.tx.operations[index].body.type())
    {
    case ACCOUNT_MERGE:
        return std::make_shared<SimulationMergeOpFrame>(op, res, *this,
                                                        resultFromArchive);
    case MANAGE_BUY_OFFER:
        return std::make_shared<SimulationManageBuyOfferOpFrame>(
            op, res, *this, resultFromArchive, mCount);
    case MANAGE_SELL_OFFER:
        return std::make_shared<SimulationManageSellOfferOpFrame>(
            op, res, *this, resultFromArchive, mCount);
    case CREATE_PASSIVE_SELL_OFFER:
        return std::make_shared<SimulationCreatePassiveSellOfferOpFrame>(
            op, res, *this, resultFromArchive, mCount);
    default:
        return OperationFrame::makeHelper(op, res, *this);
    }
}

bool
SimulationTransactionFrame::isTooEarly(LedgerTxnHeader const& header) const
{
    return mSimulationResult.result.code() == txTOO_EARLY;
}

bool
SimulationTransactionFrame::isTooLate(LedgerTxnHeader const& header) const
{
    return mSimulationResult.result.code() == txTOO_LATE;
}

bool
SimulationTransactionFrame::isBadSeq(int64_t seqNum) const
{
    return mSimulationResult.result.code() == txBAD_SEQ;
}

int64_t
SimulationTransactionFrame::getFee(LedgerHeader const& header,
                                   int64_t baseFee) const
{
    return mSimulationResult.feeCharged;
}

void
SimulationTransactionFrame::processFeeSeqNum(AbstractLedgerTxn& ltx,
                                             int64_t baseFee)
{
    mCachedAccount.reset();

    auto header = ltx.loadHeader();
    resetResults(header.current(), baseFee);

    auto sourceAccount = loadSourceAccount(ltx, header);
    if (!sourceAccount)
    {
        return;
    }
    auto& acc = sourceAccount.current().data.account();

    int64_t& fee = getResult().feeCharged;
    if (fee > 0)
    {
        fee = std::min(acc.balance, fee);
        // Note: TransactionUtil addBalance checks that reserve plus liabilities
        // are respected. In this case, we allow it to fall below that since it
        // will be caught later in commonValid.
        stellar::addBalance(acc.balance, -fee);
        header.current().feePool += fee;
    }
    // in v10 we update sequence numbers during apply
    if (header.current().ledgerVersion <= 9)
    {
        acc.seqNum = mEnvelope.v0().tx.seqNum;
    }
}

void
SimulationTransactionFrame::processSeqNum(AbstractLedgerTxn& ltx)
{
    auto header = ltx.loadHeader();
    if (header.current().ledgerVersion >= 10)
    {
        auto sourceAccount = loadSourceAccount(ltx, header);
        sourceAccount.current().data.account().seqNum =
            mEnvelope.v0().tx.seqNum;
    }
}
}
