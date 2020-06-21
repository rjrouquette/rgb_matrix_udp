//
// Created by arahasya on 6/21/20.
//

#ifndef UDP_RECEIVER_TRANSFORMING_H
#define UDP_RECEIVER_TRANSFORMING_H

namespace Transformers {
    typedef void (*Transformer) (unsigned &x, unsigned &y, unsigned &matrixWidth, unsigned &matrixHeight);

    // array of all transforming functions in order of MatrixDriver enum
    extern Transformer transformer[];

    // functions for transformer panel coordinate translation
    void NoTransforming(unsigned &x, unsigned &y, unsigned &matrixWidth, unsigned &matrixHeight);
    void MIRRORH(unsigned &x, unsigned &y, unsigned &matrixWidth, unsigned &matrixHeight);
    void MIRRORV(unsigned &x, unsigned &y, unsigned &matrixWidth, unsigned &matrixHeight);
    void ROTATE(unsigned &x, unsigned &y, unsigned &matrixWidth, unsigned &matrixHeight);
}

#endif //UDP_RECEIVER_TRANSFORMING_H
