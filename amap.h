// Copyright (c) 2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_AMAP_H
#define BITCOIN_AMAP_H

#include <vector>
#include <map>

class uint256;
class CAutoFile;

namespace amap {

extern std::string amap_path;
extern bool enabled;

typedef int64_t CAmount;
typedef std::vector<CAmount> amount_list_t;

typedef uint16_t prefix_t;
const size_t prefix_len = sizeof(prefix_t);

/**
 * Binary search for the given data `what` of the given length `what_sz` bytes
 * where each entry is `el_sz` bytes long, placing the file cursor at the end
 * of the encountered what sequence and returning true, or returning false if
 * not found.
 */
bool fbinsearch(FILE* fp, long start, long end, const uint8_t* what, size_t what_sz, size_t el_sz, bool debug = false);

/**
 * Fetch the output amount for the given transaction's output at the given index.
 * Returns -1 if the corresponding transaction could not be found.
 */
CAmount output_amount(const uint256& txid, int index);

} // namespace amap

#endif // BITCOIN_AMAP_H
