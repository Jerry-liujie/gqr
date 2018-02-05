#include <vector>
#include <map>
#include <queue>
#include <vector>
#include "lshbox/query/scoreidxpair.h"
#include <vector>
#include <unordered_map>
#include <lshbox/query/prober.h>
#include <cmath>
#include <iostream>
#include <limits>
#include <bitset>
#include <lshbox.h>
#include "lshbox/mip/lmip.h"
#include "base/bucketlist.h"

class NRTable {
public:
    typedef unsigned long long BIDTYPE;
    typedef std::unordered_map<BIDTYPE, std::vector<unsigned> > TableT;

    NRTable(
            BIDTYPE hashVal,
            unsigned paramN,
            const unsigned lengthBitNum,
            const TableT& table,
            const vector<float >& normIntervals){

        const BIDTYPE  validLengthMask = this->getValidLengthMask(lengthBitNum);

        dstToBks_.reserve(table.size());
        BIDTYPE xorVal;
        float dst;
        for ( TableT::const_iterator it = table.begin(); it != table.end(); ++it) {

            // should be improved by xor operations

            const BIDTYPE& bucketVal = it->first;
            dst  = calculateDist(hashVal, bucketVal, lengthBitNum, validLengthMask, paramN, normIntervals);

            dstToBks_.emplace_back(std::pair<float, BIDTYPE>(dst, it->first));
        }
        assert(dstToBks_.size() == table.size());

        std::sort(dstToBks_.begin(),
                  dstToBks_.end(),
                  [] (const std::pair<float, BIDTYPE>& a, const std::pair<float, BIDTYPE>& b ) {
                      return a.first < b.first;
                  });

        iterator = 0;
    }

    float getCurScore() {
        return dstToBks_[iterator].first;
    }

    BIDTYPE getCurBucket() {
        return dstToBks_[iterator].second;
    }

    // move to next, if exist return true and otherwise false
    bool moveForward() {
        iterator++;
        return iterator < dstToBks_.size();
    }

private:
    std::vector<std::pair<float, BIDTYPE> > dstToBks_;
    unsigned iterator = 0;

    BIDTYPE getValidLengthMask(unsigned lengthBitNum) {

        BIDTYPE lengthMask = 0;
        for (unsigned i = 0; i < lengthBitNum; ++i) {
            // assign the i'th bit 1
            lengthMask |= (1ULL << i);
        }

        return lengthMask;
    }

    BIDTYPE getValidBitsMask(unsigned validLength, unsigned lengthBitNum) {
        BIDTYPE bitsMask = 0;

        for (unsigned i = 0; i < validLength; ++i)
        {
            // assign the (lengthBitNum + i)'th bit 1
            bitsMask |= (1ULL << (lengthBitNum + i));
        }
        return bitsMask;
    }

    unsigned countOnes(BIDTYPE xorVal) {
        unsigned hamDist = 0;
        while(xorVal != 0){
            hamDist++;
            xorVal &= xorVal - 1;
        }
        return hamDist;
    }

    float calculateDist(
            const BIDTYPE& queryVal,
            const BIDTYPE& bucketVal,
            const unsigned lengthBitNum,
            const BIDTYPE& validLengthMask,
            const unsigned paramN,
            const vector<float >& normIntervals) {

        BIDTYPE validLength  = (bucketVal & validLengthMask) ;

        BIDTYPE validBitsMask = getValidBitsMask(paramN, lengthBitNum);
        BIDTYPE xorVal = (queryVal ^ bucketVal) &  validBitsMask;

        unsigned diffBitNum = countOnes(xorVal);
        unsigned sameBitNum = paramN - diffBitNum;
        if (validLength+1>=normIntervals.size()) {
            assert(false);
        }
        float hammingDist = (paramN / 32.0f - sameBitNum) * normIntervals[validLength+1] ;

        return hammingDist;
    }
};

template<typename ACCESSOR>
class NormalizedRank : public Prober<ACCESSOR>{
public:
    typedef typename ACCESSOR::Value value;
    typedef typename ACCESSOR::DATATYPE DATATYPE;
    typedef unsigned long long BIDTYPE;
    // typedef std::pair<float, unsigned > PairT; // <score, tableIdx>
    typedef ScoreIdxPair PairT;

    typedef lshbox::LMIP<DATATYPE> LSHTYPE;
    NormalizedRank(
            const DATATYPE* domin,
            lshbox::Scanner<ACCESSOR>& scanner,
            LSHTYPE& mylsh) : Prober<ACCESSOR>(domin, scanner, mylsh) {

        this->R_ = mylsh.getHashBitsLen();
        allTables_.reserve(mylsh.tables.size());

        for (int i = 0; i < mylsh.tables.size(); ++i) {

            BIDTYPE hashValue = mylsh.getHashVal(i, domin);
            allTables_.emplace_back(
                    NRTable(hashValue, mylsh.getHashBitsLen(), mylsh.getLengthBitsCount(), mylsh.tables[i], mylsh.getNormIntervals() ));
        }

        for (unsigned i = 0; i != allTables_.size(); ++i) {
            float score = allTables_[i].getCurScore();
            heap_.push(PairT(score , i));
        }
    }

    std::pair<unsigned, BIDTYPE> getNextBID(){
        this->numBucketsProbed_++;
        unsigned tb = heap_.top().index_;
        heap_.pop();

        BIDTYPE nextBucket = allTables_[tb].getCurBucket();
        if (allTables_[tb].moveForward()) {
            float score = allTables_[tb].getCurScore();
            heap_.push(PairT(score, tb));
        }
        return std::make_pair(tb, nextBucket);
    }

private:
    std::vector<NRTable> allTables_;

    std::priority_queue<PairT> heap_;
};