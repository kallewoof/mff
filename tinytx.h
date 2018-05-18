// Copyright (c) 2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_TINYTX_H
#define BITCOIN_TINYTX_H

#include <uint256.h>
#include <serialize.h>
#include <tinyformat.h>
#include <utilstrencodings.h>
#include <hash.h>

namespace tiny {

typedef int64_t amount;
static const amount COIN = 100000000;

static const int SERIALIZE_TRANSACTION_NO_WITNESS = 0x40000000;

struct outpoint {
    uint256 hash;
    uint32_t n;
    outpoint() : n((uint32_t)-1) {}
    outpoint(const uint256& hash_in, uint32_t n_in) : hash(hash_in), n(n_in) {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(hash);
        READWRITE(n);
    }

    bool IsNull() const { return (hash.IsNull() && n == (uint32_t) -1); }

    std::string ToString() const { return strprintf("outpoint(%s, %u)", hash.ToString().substr(0,10), n); }
};

typedef std::vector<uint8_t> script_data_t;
typedef std::vector<std::vector<uint8_t>> script_stack_t;

struct txin {
    outpoint prevout;
    script_data_t scriptSig;
    uint32_t sequence;
    script_stack_t scriptWit;

    static const uint32_t SEQUENCE_FINAL = 0xffffffff;

    txin() : sequence(SEQUENCE_FINAL) {}

    txin(outpoint prevout_in, script_data_t scriptSig_in=script_data_t(), uint32_t sequence_in=SEQUENCE_FINAL) {
        prevout = prevout_in;
        scriptSig = scriptSig_in;
        sequence = sequence_in;
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(prevout);
        READWRITE(scriptSig);
        READWRITE(sequence);
    }

    std::string scriptWitString() const {
        std::string ret = "scriptWit(";
        for (unsigned int i = 0; i < scriptWit.size(); i++) {
            if (i) {
                ret += ", ";
            }
            ret += HexStr(scriptWit[i]);
        }
        return ret + ")";
    }

    std::string ToString() const {
        std::string str;
        str += "txin(";
        str += prevout.ToString();
        if (prevout.IsNull()) {
            str += strprintf(", coinbase %s", HexStr(scriptSig));
        } else {
            str += strprintf(", scriptSig=%s", HexStr(scriptSig).substr(0, 24));
        }
        if (sequence != SEQUENCE_FINAL) {
            str += strprintf(", sequence=%u", sequence);
        }
        str += ")";
        return str;
    }
};

struct txout {
    amount value;
    script_data_t scriptPubKey;

    txout() : value(-1) {}
    txout(const amount& value_in, script_data_t scriptPubKey_in) : value(value_in), scriptPubKey(scriptPubKey_in) {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(value);
        READWRITE(scriptPubKey);
    }

    std::string ToString() const { 
        return strprintf("txout(value=%d.%08d, scriptPubKey=%s)", value / COIN, value % COIN, HexStr(scriptPubKey).substr(0, 30));
    }
};

/**
* Basic transaction serialization format:
* - int32_t nVersion
* - std::vector<CTxIn> vin
* - std::vector<CTxOut> vout
* - uint32_t nLockTime
*
* Extended transaction serialization format:
* - int32_t nVersion
* - unsigned char dummy = 0x00
* - unsigned char flags (!= 0)
* - std::vector<CTxIn> vin
* - std::vector<CTxOut> vout
* - if (flags & 1):
*   - CTxWitness wit;
* - uint32_t nLockTime
*/

struct tx {
    std::vector<txin> vin;
    std::vector<txout> vout;
    int32_t version;
    uint32_t locktime;

    uint256 hash;

    tx() : vin(), vout(), version(2), locktime(0), hash() {}

    friend bool operator==(const tx& a, const tx& b)
    {
        return a.hash == b.hash;
    }

    void UpdateHash() {
        hash = SerializeHash(*this, SER_GETHASH, SERIALIZE_TRANSACTION_NO_WITNESS);
    }

    bool HasWitness() const
    {
        for (size_t i = 0; i < vin.size(); i++) {
            if (vin[i].scriptWit.size()) {
                return true;
            }
        }
        return false;
    }

    bool IsCoinBase() const
    {
        return (vin.size() == 1 && vin[0].prevout.IsNull());
    }

    template<typename Stream>
    inline void Unserialize(Stream& s) {
        s >> version;
        unsigned char flags = 0;
        vin.clear();
        vout.clear();
        /* Try to read the vin. In case the dummy is there, this will be read as an empty vector. */
        s >> vin;
        if (vin.size() == 0) {
            /* We read a dummy or an empty vin. */
            s >> flags;
            if (flags != 0) {
                s >> vin;
                s >> vout;
            }
        } else {
            /* We read a non-empty vin. Assume a normal vout follows. */
            s >> vout;
        }
        if ((flags & 1)) {
            /* The witness flag is present, and we support witnesses. */
            flags ^= 1;
            for (size_t i = 0; i < vin.size(); i++) {
                s >> vin[i].scriptWit;
            }
        }
        if (flags) {
            /* Unknown flag in the serialization */
            throw std::ios_base::failure("Unknown transaction optional data");
        }
        s >> locktime;
        UpdateHash();
    }

    template<typename Stream>
    inline void Serialize(Stream& s) const {
        s << version;
        unsigned char flags = 0;
        // Consistency check
        /* Check whether witnesses need to be serialized. */
        if (HasWitness()) {
            flags |= 1;
        }
        if (flags) {
            /* Use extended format in case witnesses are to be serialized. */
            std::vector<txin> vinDummy;
            s << vinDummy;
            s << flags;
        }
        s << vin;
        s << vout;
        if (flags & 1) {
            for (size_t i = 0; i < vin.size(); i++) {
                s << vin[i].scriptWit;
            }
        }
        s << locktime;
    }

    std::string ToString() const {
        std::string str;
        str += strprintf("tx(hash=%s, ver=%d, vin.size=%u, vout.size=%u, locktime=%u)\n",
            hash.ToString().substr(0,10),
            version,
            vin.size(),
            vout.size(),
            locktime);
        for (const auto& tx_in : vin)
            str += "    " + tx_in.ToString() + "\n";
        for (const auto& tx_in : vin)
            str += "    " + tx_in.scriptWitString() + "\n";
        for (const auto& tx_out : vout)
            str += "    " + tx_out.ToString() + "\n";
        return str;
    }
};

}

#endif // BITCOIN_TINYTX_H
