#include "gabac/lut_transform.h"

#include <algorithm>
#include <cassert>
#include <iostream>
#include <limits>
#include <unordered_map>
#include <utility>

#include "gabac/return_codes.h"


// ----------------------------------------------------------------------------
// C wrapper BEGIN
// ----------------------------------------------------------------------------

/*int gabac_transformLutTransform0(
        const uint64_t *const symbols,
        const size_t symbolsSize,
        uint64_t **const transformedSymbols,
        uint64_t **const inverseLUT,
        size_t *const inverseLUTSize
){
    if (symbols == nullptr ||
        transformedSymbols == nullptr ||
        inverseLUT == nullptr ||
        inverseLUTSize == nullptr)
    {
        return GABAC_FAILURE;
    }

    DataStream symbolsVecCpp(symbols, symbols + symbolsSize);
    DataStream transformedVec;
    DataStream invLutVec;

    gabac::transformLutTransform0(symbolsVecCpp, &transformedVec, &invLutVec);

    (*transformedSymbols) = static_cast<uint64_t *>(malloc(sizeof(uint64_t) * transformedVec.size()));
    (*inverseLUT) = static_cast<uint64_t *>(malloc(sizeof(uint64_t) * invLutVec.size()));

    std::copy(transformedVec.begin(), transformedVec.end(), *transformedSymbols);
    std::copy(invLutVec.begin(), invLutVec.end(), *inverseLUT);

    *inverseLUTSize = invLutVec.size();

    return GABAC_SUCCESS;
}

// ----------------------------------------------------------------------------

int gabac_inverseTransformLutTransform0(
        const uint64_t *transformedSymbols,
        size_t transformedSymbolsSize,
        const uint64_t *inverseLUT,
        size_t inverseLUTSize,
        uint64_t **symbols
){
    if (transformedSymbols == nullptr ||
        inverseLUT == nullptr ||
        symbols == nullptr)
    {
        return GABAC_FAILURE;
    }

    DataStream transSymVec(transformedSymbols, transformedSymbols + transformedSymbolsSize);
    DataStream invLutVec(inverseLUT, inverseLUT + inverseLUTSize);
    DataStream symVec;

    gabac::inverseTransformLutTransform0(transSymVec, invLutVec, &symVec);

    (*symbols) = static_cast<uint64_t *>(malloc(sizeof(uint64_t) * symVec.size()));

    std::copy(symVec.begin(), symVec.end(), *symbols);

    return GABAC_SUCCESS;
}*/

// ----------------------------------------------------------------------------
// C wrapper END
// ----------------------------------------------------------------------------

namespace gabac {

const size_t MAX_LUT_SIZE = 1u << 20u; // 8MB table


// Inplace not reasonable, because symbols stream is reused 100% of time and lut is much smaller
static void inferLut0(
        const DataStream& symbols,
        std::vector<std::pair<uint64_t, uint64_t>> *const lut,
        DataStream *const fastlut,
        DataStream *const inverseLut
){

    uint64_t maxValue = 0;

    // At some point it is more efficient to use an hashmap instead of an array
    const uint64_t CTR_THRESHOLD = 1000000;

    // Wordsize 1 and 2 are small enough to use the array every time
    if (symbols.getWordSize() == 1) {
        maxValue = std::numeric_limits<uint8_t>::max();
    } else if (symbols.getWordSize() == 2) {
        maxValue = std::numeric_limits<uint16_t>::max();
    } else {
        // For greater wordsizes: find max and check if small enough for array counter
        for (size_t i = 0; i < symbols.size(); ++i) {
            uint64_t val = symbols.get(i);
            if (val > maxValue) {
                maxValue = val;
            }
            if (val >= CTR_THRESHOLD) {
                break;
            }
        }
        // Step 1: basic mapping for order 0. All symbols are now in a dense interval starting [0...N]

    }

    // Clear
    lut->clear();
    inverseLut->clear();
    if (symbols.empty()) {
        return;
    }


    std::vector<std::pair<uint64_t, uint64_t>> freqVec;

    if (maxValue < CTR_THRESHOLD) {
        std::vector<uint64_t> freq(maxValue + 1);
        for (size_t i = 0; i < symbols.size(); ++i) {
            uint64_t symbol = symbols.get(i);
            freq[symbol]++;
        }
        for (size_t i = 0; i < freq.size(); ++i) {
            if (freq[i]) {
                freqVec.emplace_back(uint64_t(i), freq[i]);
            }
        }
    } else {
        std::unordered_map<uint64_t, uint64_t> freq;
        for (size_t i = 0; i < symbols.size(); ++i) {
            uint64_t symbol = symbols.get(i);
            freq[symbol]++;
            if (freq.size() >= MAX_LUT_SIZE) {
                return;
            }
        }
        std::copy(freq.begin(), freq.end(), std::back_inserter(freqVec));
    }


    // Sort symbol frequencies in descending order
    std::sort(
            freqVec.begin(), freqVec.end(),
            [](const std::pair<uint64_t, uint64_t>& a,
               const std::pair<uint64_t, uint64_t>& b
            )
            {
                if (a.second > b.second) {
                    return true;
                }
                if (a.second < b.second) {
                    return false;
                }
                return a.first < b.first;
            }
    );


    for (const auto& symbol : freqVec) {
        lut->emplace_back(symbol.first, inverseLut->size());
        inverseLut->emplace_back(symbol.first);
    }


    // Sort symbols
    std::sort(
            lut->begin(), lut->end(),
            [](const std::pair<uint64_t, uint64_t>& a,
               const std::pair<uint64_t, uint64_t>& b
            )
            {
                if (a.first < b.first) {
                    return true;
                }
                if (a.first > b.first) {
                    return false;
                }
                return a.second > b.second;
            }
    );

    if (maxValue < CTR_THRESHOLD) {
        fastlut->resize(maxValue + 1);
        for (auto p : *lut) {
            (*fastlut).set(p.first, p.second);
        }
    }

}

// ----------------------------------------------------------------------------

static uint64_t lut0SingleTransform(
        const std::vector<std::pair<uint64_t, uint64_t>>& lut0,
        uint64_t symbol
){
    auto it = std::lower_bound(
            lut0.begin(), lut0.end(), std::make_pair(symbol, uint(0)),
            [](const std::pair<uint64_t, uint64_t>& a,
               const std::pair<uint64_t, uint64_t>& b
            )
            {
                return a.first < b.first;
            }
    );
    assert(it != lut0.end());
    assert(it->first == symbol);
    return it->second;
}

// ----------------------------------------------------------------------------

static uint64_t lut0SingleTransformFast(
        const DataStream& lut0,
        uint64_t symbol
){
    return lut0.get(symbol);
}

// ----------------------------------------------------------------------------

static void transformLutTransform_core(
        const size_t ORDER,
        const std::vector<std::pair<uint64_t, uint64_t>>& lut0,
        const DataStream& fastlut,
        const DataStream& lut,
        DataStream *const transformedSymbols
){
    assert(transformedSymbols != nullptr);

    if (transformedSymbols->empty()) {
        return;
    }

    std::vector<uint64_t> lastSymbols(ORDER + 1, 0);


    // Do the LUT transform
    for (size_t i = 0; i < transformedSymbols->size(); ++i) {
        uint64_t symbol = transformedSymbols->get(i);
        // Update history
        for (size_t j = ORDER; j > 0; --j) {
            lastSymbols[j] = lastSymbols[j - 1];
        }
        if (fastlut.size()) {
            lastSymbols[0] = lut0SingleTransformFast(fastlut, symbol);
        } else {
            lastSymbols[0] = lut0SingleTransform(lut0, symbol);
        }

        // Transform
        uint64_t transformed = lastSymbols[0];
        if (ORDER > 0) {
            // Compute position
            size_t index = 0;
            for (size_t j = ORDER; j > 0; --j) {
                index *= lut0.size();
                index += lastSymbols[j];
            }
            index *= lut0.size();
            index += lastSymbols[0];
            transformed = lut.get(index);
        }
        transformedSymbols->set(i, transformed);
    }
}

// ----------------------------------------------------------------------------

static void inverseTransformLutTransform_core(
        const size_t ORDER,
        DataStream *const symbols,
        DataStream *const inverseLut0,
        DataStream *const inverseLut
){
    assert(symbols != nullptr);


    std::vector<uint64_t> lastSymbols(ORDER + 1, 0);

    // Do the LUT transform
    for (size_t i = 0; i < symbols->size(); ++i) {
        uint64_t symbol = symbols->get(i);
        // Update history
        for (size_t j = ORDER; j > 0; --j) {
            lastSymbols[j] = lastSymbols[j - 1];
        }
        lastSymbols[0] = static_cast<uint64_t>(symbol);

        if (ORDER == 0) {
            symbols->set(i, inverseLut0->get(lastSymbols[0]));
            continue;
        }

        // Compute position
        size_t index = 0;
        for (size_t j = ORDER; j > 0; --j) {
            index *= inverseLut0->size();
            index += lastSymbols[j];
        }
        index *= inverseLut0->size();
        index += lastSymbols[0];

        // Transform
        uint64_t unTransformed = inverseLut->get(index);
        lastSymbols[0] = unTransformed;
        symbols->set(i, inverseLut0->get(unTransformed));
    }
}

void inferLut(
        const size_t ORDER,
        const DataStream& symbols,
        std::vector<std::pair<uint64_t, uint64_t>> *const lut0,
        DataStream *const fastlut,
        DataStream *const inverseLut0,
        DataStream *const lut1,
        DataStream *const inverseLut1
){
    // Clear
    lut1->clear();
    inverseLut1->clear();

    inferLut0(symbols, lut0, fastlut, inverseLut0);

    if (symbols.empty()) {
        return;
    }

    if (ORDER == 0) {
        return;
    }

    size_t size = 1;
    for (size_t i = 0; i < ORDER + 1; ++i) {
        size *= inverseLut0->size();
    }

    if (size >= MAX_LUT_SIZE) {
        lut0->clear();
        return;
    }

    std::vector<std::pair<uint64_t, uint64_t>> ctr(size, {std::numeric_limits<uint64_t>::max(), 0});
    std::vector<uint64_t> lastSymbols(ORDER + 1, 0);

    for (size_t i = 0; i < symbols.size(); ++i) {
        uint64_t symbol = symbols.get(i);
        // Update history
        for (size_t j = ORDER; j > 0; --j) {
            lastSymbols[j] = lastSymbols[j - 1];
        }

        // Translate symbol into order1 symbol
        uint64_t narrowedSymbol = symbol;
        lastSymbols[0] = lut0SingleTransform(*lut0, narrowedSymbol);

        // Compute position
        size_t index = 0;
        for (size_t j = ORDER; j > 0; --j) {
            index *= inverseLut0->size();
            index += lastSymbols[j];
        }
        index *= inverseLut0->size();
        index += lastSymbols[0];


        // Count
        ctr[index].second++;
    }

    // Step through all single LUTs
    for (size_t i = 0; i < ctr.size(); i += inverseLut0->size()) {
        uint64_t counter = 0;
        for (auto it = ctr.begin() + i; it != ctr.begin() + i + inverseLut0->size(); ++it) {
            it->first = counter;
            counter++;
        }

        // Sort single LUT for frequency
        std::sort(
                ctr.begin() + i, ctr.begin() + i + inverseLut0->size(),
                [](const std::pair<uint64_t, uint64_t>& a,
                   const std::pair<uint64_t, uint64_t>& b
                )
                {
                    if (a.second > b.second) {
                        return true;
                    }
                    if (a.second < b.second) {
                        return false;
                    }
                    return a.first < b.first;
                }
        );

        // Fill inverseLUT and write rank into second field
        counter = 0;
        bool placed = false;
        for (auto it = ctr.begin() + i; it != ctr.begin() + i + inverseLut0->size(); ++it) {
            if (it->second == 0 && !placed) {
                placed = true;
            }
            if (!placed) {
                inverseLut1->emplace_back(it->first);
            } else {
                inverseLut1->emplace_back(0);
            }
            it->second = counter;
            counter++;
        }


        // Sort single LUT for symbol value
        std::sort(
                ctr.begin() + i, ctr.begin() + i + inverseLut0->size(),
                [](const std::pair<uint64_t, uint64_t>& a,
                   const std::pair<uint64_t, uint64_t>& b
                )
                {
                    if (a.first < b.first) {
                        return true;
                    }
                    if (a.first > b.first) {
                        return false;
                    }
                    return a.second > b.second;
                }
        );

        // Use previously set second field to fill LUT
        for (auto it = ctr.begin() + i; it != ctr.begin() + i + inverseLut0->size(); ++it) {
            lut1->emplace_back(it->second);
        }
    }
}
// ----------------------------------------------------------------------------

void transformLutTransform0(
        unsigned order,
        DataStream *const transformedSymbols,
        DataStream *const inverseLUT,
        DataStream *const inverseLUT1
){
    std::vector<std::pair<uint64_t, uint64_t>> lut;
    DataStream fastlut(0, transformedSymbols->getWordSize()); // For small, dense symbol spaces
    DataStream lut1(0, transformedSymbols->getWordSize());
    inferLut(order, *transformedSymbols, &lut, &fastlut, inverseLUT, &lut1, inverseLUT1);
    if (lut.empty()) {
        inverseLUT->clear();
        inverseLUT1->clear();
        return;
    }
    transformLutTransform_core(order, lut, fastlut, lut1, transformedSymbols);
}

// ----------------------------------------------------------------------------

void inverseTransformLutTransform0(
        unsigned order,
        DataStream *const symbols,
        DataStream *const inverseLUT,
        DataStream *const inverseLUT1
){
    inverseTransformLutTransform_core(order, symbols, inverseLUT, inverseLUT1);
}

// ----------------------------------------------------------------------------

}  // namespace gabac

// ----------------------------------------------------------------------------
// ----------------------------------------------------------------------------
