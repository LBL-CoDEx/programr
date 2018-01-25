#include "byteseq.hxx"

using namespace programr;
using namespace std;

int ByteSeqIter_::next(int buf_sz, uint8_t *buf_byte, int *buf_ix) {
  //const std::uint8_t *_seq = this->ptr;
  //int _ix = 0; // decoded byte index
  
  int n = 0;
  
  switch(_label) {
  case 0:
    while(true) {
      //std::uint8_t _head;
      _head = *_seq++;
      
      //int _i;
      for(_i=0; _i < 8; _i++) {
        {
          int head_bit = _head & 1;
          _head >>= 1;
          //std::uint8_t _byte;
          _byte = *_seq++;
          
          if(head_bit == 1 && _byte == 0) { // eof
            _label = 2;
            return n;
          }
          
          if(head_bit == 0) {
            std::uint8_t chunk = _byte;
            int chunk_bits;
            if(_byte & 0x80) {
              chunk_bits = 4;
              _byte = 1<<(_byte>>4 & 0x07);
              _ix += 1;
            }
            else {
              chunk_bits = 7;
              _byte = 0;
            }
            
            int mag = 0;
            while(true) {
              _ix += (chunk & ((1<<(chunk_bits-1))-1)) << mag;
              mag += chunk_bits-1;
              if(0 == (chunk & (1<<(chunk_bits-1))))
                break;
              chunk = *_seq++;
              chunk_bits = 8;
            }
          }
        }
        
        if(_byte != 0) {
          if(buf_sz == 0) {
            // yield
            _label = 1;
            return n;
          }
  case 1:
          *buf_ix++ = _ix;
          *buf_byte++ = _byte;
          buf_sz -= 1;
          n += 1;
        }
        
        _ix += 1;
      }
    }
    
  default:
    return 0;
  }
}
