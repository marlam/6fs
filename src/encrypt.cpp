/*
 * Copyright (C) 2023
 * Martin Lambers <marlam@marlam.de>
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 3 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <cstring>
#include <cerrno>

#include "encrypt.hpp"


void enc(const unsigned char* key, const unsigned char* msg, size_t msgSize, unsigned char* out)
{
    out[0] = 255; // all bits always set in the first byte to be able to detect chunks that were turned into holes
    unsigned char* nonce = out + 1;
    unsigned char* ciphertext = out + 1 + crypto_secretbox_NONCEBYTES;
    randombytes_buf(nonce, crypto_secretbox_NONCEBYTES);
    crypto_secretbox_easy(ciphertext, msg, msgSize, nonce, key);
}

int dec(const unsigned char* key, const unsigned char* in, size_t inSize, unsigned char* msg, size_t msgSize)
{
    const unsigned char* nonce = in + 1;
    const unsigned char* ciphertext = in + 1 + crypto_secretbox_NONCEBYTES;
    const size_t ciphertextLen = inSize - crypto_secretbox_NONCEBYTES - 1;
    int r = 0;
    if (in[0] == 0) {
        // this chunk was turned into a hole: the cleartext data is all zero
        ::memset(msg, 0, msgSize);
    } else {
        if (crypto_secretbox_open_easy(msg, ciphertext, ciphertextLen, nonce, key) != 0) {
            r = -EIO;
        }
    }
    return r;
}
