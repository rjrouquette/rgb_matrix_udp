//
// Created by arahasya on 6/21/20.
//

#include "Transforming.h"

// array of all transforming functions in order of MatrixDriver enum
Transformers::Transformer Transformers::transformer[4] = {
        Transformers::NoTransforming,
	Transformers::MIRRORH,
	Transformers::MIRRORV,
	Transformers::ROTATE
};

// no transforming does nothing
void Transformers::NoTransforming(unsigned int &x, unsigned int &y, unsigned &matrixWidth, unsigned &matrixHeight) {

}

// horizontal mirror transformer
void Transformers::MIRRORH(unsigned &x, unsigned &y, unsigned &matrixWidth, unsigned &matrixHeight) {
	x = matrixWidth - 1 - x;
	y = y ;
}

// vertical mirror transformer
void Transformers::MIRRORV(unsigned &x, unsigned &y, unsigned &matrixWidth, unsigned &matrixHeight) {
}

// rotational transformer
void Transformers::ROTATE(unsigned &x, unsigned &y, unsigned &matrixWidth, unsigned &matrixHeight) {
}
