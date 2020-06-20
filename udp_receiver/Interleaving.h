//
// Created by robert on 6/20/20.
//

#ifndef UDP_RECEIVER_INTERLEAVING_H
#define UDP_RECEIVER_INTERLEAVING_H

namespace Interleavers {
    typedef void (*Interleaver) (unsigned &x, unsigned &y);

    void NoInterleaving(unsigned &x, unsigned &y);
    void Z32ABC(unsigned &x, unsigned &y);
    void Z16AB(unsigned &x, unsigned &y);
    void Z08AB(unsigned &x, unsigned &y);

    extern Interleaver interleaver[];


    void dimNoInterleaving(unsigned &x, unsigned &y);
    void dimZ32ABC(unsigned &x, unsigned &y);
    void dimZ16AB(unsigned &x, unsigned &y);
    void dimZ08AB(unsigned &x, unsigned &y);

    extern Interleaver dimensions[];
}

#endif //UDP_RECEIVER_INTERLEAVING_H
