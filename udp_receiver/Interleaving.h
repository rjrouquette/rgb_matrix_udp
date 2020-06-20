//
// Created by robert on 6/20/20.
//

#ifndef UDP_RECEIVER_INTERLEAVING_H
#define UDP_RECEIVER_INTERLEAVING_H

namespace Interleavers {
    typedef void (*Interleaver) (unsigned &x, unsigned &y);

    // array of all interleaver functions in order of MatrixDriver enum
    extern Interleaver interleaver[];

    // array of all dimension translation functions in order of MatrixDriver enum
    extern Interleaver dimensions[];

    // functions for interleaved panel coordinate translation
    void NoInterleaving(unsigned &x, unsigned &y);
    void Z32ABC(unsigned &x, unsigned &y);
    void Z16AB(unsigned &x, unsigned &y);
    void Z08AB(unsigned &x, unsigned &y);

    // functions for interleaved panel raster dimension translation
    void dimNoInterleaving(unsigned &x, unsigned &y);
    void dimZ32ABC(unsigned &x, unsigned &y);
    void dimZ16AB(unsigned &x, unsigned &y);
    void dimZ08AB(unsigned &x, unsigned &y);
}

#endif //UDP_RECEIVER_INTERLEAVING_H
