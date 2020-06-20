//
// Created by robert on 6/20/20.
//

#include "Interleaving.h"

// array of all interleaver functions in order of MatrixDriver enum
Interleavers::Interleaver Interleavers::interleaver[4] = {
        Interleavers::NoInterleaving,
        Interleavers::Z32ABC,
        Interleavers::Z16AB,
        Interleavers::Z08AB
};

// no interleaving does nothing
void Interleavers::NoInterleaving(unsigned int &x, unsigned int &y) {

}

// Z-striped 3-bit address 32-pixel stripe
void Interleavers::Z32ABC(unsigned &x, unsigned &y) {
    // determine interleaving offset
    auto offset = (y & 8u) ^ 8u;
    // interleave y-coordinate
    y = (y & 0x7u) | ((y & 0xfffffff0u) >> 1u);
    // interleave x-coordinate: leave lower 5 bits intact, shift upper bits, and add offset
    x = (x & 0x1fu) | ((x & 0x7fffffe0u) << 1u) | (offset << 2u);
}

// Z-striped 2-bit address 16-pixel stripe
void Interleavers::Z16AB(unsigned &x, unsigned &y) {
    // determine interleaving offset
    auto offset = (y & 4u) ^ 4u;
    // interleave y-coordinate
    y = (y & 0x3u) | ((y & 0xfffffff8u) >> 1u);
    // interleave x-coordinate: leave lower 4 bits intact, shift upper bits, and add offset
    x = (x & 0xfu) | ((x & 0x7ffffff0u) << 1u) | (offset << 2u);
}

// Z-striped 2-bit address 8-pixel stripe
void Interleavers::Z08AB(unsigned &x, unsigned &y) {
    // determine interleaving offset
    auto offset = (y & 4u) ^ 4u;
    // interleave y-coordinate
    y = (y & 0x3u) | ((y & 0xfffffff8u) >> 1u);
    // interleave x-coordinate: leave lower 4 bits intact, shift upper bits, and add offset
    x = (x & 0x7u) | ((x & 0x7ffffff8u) << 1u) | (offset << 1u);
}



// array of all dimension translation functions in order of MatrixDriver enum
Interleavers::Interleaver Interleavers::dimensions[4] = {
        Interleavers::dimNoInterleaving,
        Interleavers::dimZ32ABC,
        Interleavers::dimZ16AB,
        Interleavers::dimZ08AB
};

// no interleaving does nothing
void Interleavers::dimNoInterleaving(unsigned int &width, unsigned int &height) {

}

// Z-striped 3-bit address 32-pixel stripe
void Interleavers::dimZ32ABC(unsigned &width, unsigned &height) {
    width /= 2;
    height *= 2;
}

// Z-striped 2-bit address 16-pixel stripe
void Interleavers::dimZ16AB(unsigned &width, unsigned &height) {
    width /= 2;
    height *= 2;
}

// Z-striped 2-bit address 8-pixel stripe
void Interleavers::dimZ08AB(unsigned &width, unsigned &height) {
    width /= 2;
    height *= 2;
}
