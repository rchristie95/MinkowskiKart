//
//  MinkowskiKart - a fun racing game with go-kart
//  Copyright (C) 2018 MinkowskiKart-Team
//
//  This program is free software; you can redistribute it and/or
//  modify it under the terms of the GNU General Public License
//  as published by the Free Software Foundation; either version 3
//  of the License, or (at your option) any later version.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with this program; if not, write to the Free Software
//  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

#ifndef HEADER_CRYPTO_HPP
#define HEADER_CRYPTO_HPP

#ifdef APPLE_NETWORK_LIBRARIES
#include "network/crypto_cryptokit.hpp"
#elif defined(ENABLE_CRYPTO_OPENSSL)
#include "network/crypto_openssl.hpp"
#else
#include "network/crypto_mbedtls.hpp"
#endif

#include "network/network_string.hpp"

#include <stdexcept>

inline bool decodeAlohaCredentials(const std::string& key_text,
                                   const std::string& iv_text,
                                   std::vector<uint8_t>& key,
                                   std::vector<uint8_t>& iv)
{
    // 16-byte keys encode to 24 characters with two trailing '=', while a
    // 12-byte IV encodes to exactly 16 alphabet characters. Validate before
    // calling backend decoders, some of which assume nonempty padded input.
    if (key_text.size() != 24 || iv_text.size() != 16 ||
        key_text[22] != '=' || key_text[23] != '=')
        return false;
    auto value = [](char c) -> int
    {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+') return 62;
        if (c == '/') return 63;
        return -1;
    };
    for (size_t i = 0; i < 22; i++)
        if (value(key_text[i]) < 0)
            return false;
    // Canonical base64 requires the unused low four bits to be zero.
    if ((value(key_text[21]) & 0x0f) != 0)
        return false;
    for (char c : iv_text)
        if (value(c) < 0)
            return false;

    std::vector<uint8_t> decoded_key = Crypto::decode64(key_text);
    std::vector<uint8_t> decoded_iv = Crypto::decode64(iv_text);
    if (decoded_key.size() != 16 || decoded_iv.size() != 12)
        return false;
    key.swap(decoded_key);
    iv.swap(decoded_iv);
    return true;
}

// Derive an independent key for authenticated Aloha discovery messages.
// Keeping this key separate avoids reusing the AES-GCM key used by the game
// connection protocol.
inline std::vector<uint8_t> getAlohaAuthKey(
    const std::vector<uint8_t>& client_key)
{
    static const char context[] = "MinkowskiKart Aloha authentication v1";
    std::string input(context, sizeof(context) - 1);
    input.append(reinterpret_cast<const char*>(client_key.data()),
                 client_key.size());
    const std::array<uint8_t, 32> digest = Crypto::sha256(input);
    return std::vector<uint8_t>(digest.begin(), digest.begin() + 16);
}

inline void cryptoShortPacketUnitTesting()
{
    const std::vector<uint8_t> key(16, 0x23);
    const std::vector<uint8_t> iv(12, 0x45);
    Crypto crypto(key, iv, 16);
    const size_t invalid_lengths[] = { 0, 4, 19, 20 };
    for (size_t length : invalid_lengths)
    {
        ENetPacket* packet = enet_packet_create(NULL, length, 0);
        if (!packet)
            throw std::runtime_error("Could not allocate crypto test packet.");
        bool rejected = false;
        try
        {
            std::unique_ptr<NetworkString> decrypted(
                crypto.decryptRecieve(packet));
        }
        catch (const std::exception&)
        {
            rejected = true;
        }
        enet_packet_destroy(packet);
        if (!rejected)
            throw std::runtime_error("Crypto accepted a short packet.");
    }
}

#endif // HEADER_CRYPTO_HPP
