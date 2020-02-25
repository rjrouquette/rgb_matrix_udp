//
// Created by robert on 2/25/20.
//

#ifndef UDP_RECEIVER_ROWENCODING_H
#define UDP_RECEIVER_ROWENCODING_H

namespace RowEncoding {
    typedef unsigned (*Encoder) (unsigned pwmRows, unsigned srow, unsigned idx);

    unsigned Qiangli_Q3F32(unsigned pwmRows, unsigned srow, unsigned idx);
    unsigned Hub75(unsigned pwmRows, unsigned srow, unsigned idx);
    unsigned Hub75e(unsigned pwmRows, unsigned srow, unsigned idx);

    extern Encoder encoder[];
}

#endif //UDP_RECEIVER_ROWENCODING_H
