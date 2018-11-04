#include <memory.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define COUNT_MAX 31
#define JUMP_MAX 2047

struct mem_block {
  size_t size;
  uint8_t* addr;
  size_t memsize_for_decompressed(void) const {
    if ((size == 0) || (addr == NULL) || (memcmp(addr, "LZTXT", 5) != 0)) {
      return 0;
    }
    return *(uint16_t*)&addr[5];
  }
  size_t memsize_for_compressed(void) const {
    if (size > 65535) {
      return 0;
    }
    return size + (size / JUMP_MAX + 2) * 2 + 7;
  }
};

static mem_block read_entire_file_into_memory(const char* file_name) {
  mem_block result = {};

  FILE* file = fopen(file_name, "r");

  if (file) {
    fseek(file, 0, SEEK_END);
    result.size = ftell(file);
    fseek(file, 0, SEEK_SET);

    result.addr = (uint8_t*)malloc(result.size);

    if (result.addr == NULL)
      return {0, NULL};

    fread(result.addr, result.size, 1, file);

    fclose(file);
  }

  return result;
}

static void write_memory_to_file(mem_block data, const char* file_name) {
  FILE* file = fopen(file_name, "wb");

  if (file) {
    fwrite(data.addr, data.size, 1, file);
    fclose(file);
  }
}

void decompress(mem_block compressed, mem_block& result) {
  for (size_t dst_off = 0, src_off = 7;
       src_off < compressed.size;) {  // increase the offset manually
    uint16_t word = *(uint16_t*)&compressed.addr[src_off];
    uint16_t jump = word >> 5;
    uint16_t count = word & 31;
    src_off += 2;
    if (count == 0) {
      // new data, jump is number of bytes
      memcpy(&result.addr[dst_off], &compressed.addr[src_off], jump);
      dst_off += jump;
      src_off += jump;
    } else {
      // refer to available data. input memory is not used
      for (int runner = 0; runner < count; ++runner, ++dst_off) {
        result.addr[dst_off] = result.addr[dst_off - jump];
      }
    }
  }
}

void compress(mem_block block, mem_block& dest) {
  uint16_t unique_len = 0;
  size_t dest_offset = 0;
  dest.addr[dest_offset++] = 'L';
  dest.addr[dest_offset++] = 'Z';
  dest.addr[dest_offset++] = 'T';
  dest.addr[dest_offset++] = 'X';
  dest.addr[dest_offset++] = 'T';
  dest.addr[dest_offset++] = (char)(block.size & 255);  // lower byte
  dest.addr[dest_offset++] = (char)(block.size >> 8);   // upper byte

  for (uint16_t pos = 0; pos < block.size;) {
    // check the string at every position
    uint16_t repeat_len = 0;
    uint16_t repeat_jmp = 0;

    // find the longest repetition info to repeat_len and repeat_jmp
    for (uint16_t jmp = 1; ((jmp < JUMP_MAX) && (jmp <= pos)); jmp++) {
      uint16_t len = 0;
      while ((len < COUNT_MAX) && (pos + len < block.size) &&
             (block.addr[pos - jmp + len] == block.addr[pos + len])) {
        len++;
      }
      if (len > repeat_len) {
        repeat_len = len;
        repeat_jmp = jmp;
      }
    }

    // output
    if (repeat_len > 2) {  // 2 bytes is just the overhead
      // 1. output the collected unique (non-repeated) data if present
      if (unique_len > 0) {
        // pack 11bit length (0 for second part) to 16bit word

        unique_len <<= 5;

        dest.addr[dest_offset++] = (char)(unique_len & 255);  // lower byte
        dest.addr[dest_offset++] = (char)(unique_len >> 8);   // upper byte
        memcpy(&dest.addr[dest_offset], &block.addr[pos - (unique_len >> 5)],
               unique_len >> 5);
        dest_offset += (unique_len >> 5);
      }

      // 2. output the repeated block
      uint16_t word = (repeat_jmp << 5) | repeat_len;

      dest.addr[dest_offset++] = (char)(word & 255);  // lower byte
      dest.addr[dest_offset++] = (char)(word >> 8);   // upper byte

      pos += repeat_len;
      unique_len = 0;
    } else {
      unique_len++;
      pos++;
      if ((unique_len == JUMP_MAX) || (pos == block.size)) {
        // output the last uncompressed one or hit the limit of the length
        unique_len <<= 5;
        dest.addr[dest_offset++] = (char)(unique_len & 255);  // lower byte
        dest.addr[dest_offset++] = (char)(unique_len >> 8);   // upper byte
        memcpy(&dest.addr[dest_offset], &block.addr[pos - (unique_len >> 5)],
               unique_len >> 5);
        dest_offset += (unique_len >> 5);
        unique_len = 0;
      }
    }
  }

  if (dest_offset > dest.size) {
    fprintf(stderr, "Crap\n");  // overflow already happened
  }
  dest.size = dest_offset;  // free will release correctly anyway
}

int main(int argc, char** argv) {
  if (argc == 2) {
    mem_block block = read_entire_file_into_memory(argv[1]);

    if (block.addr != NULL) {
      mem_block lz_data;
      lz_data.size = block.memsize_for_compressed();
      lz_data.addr = (uint8_t*)malloc(lz_data.size);
      compress(block, lz_data);

      mem_block decompressed;
      decompressed.size = lz_data.memsize_for_decompressed();
      decompressed.addr = (uint8_t*)malloc(decompressed.size);
      decompress(lz_data, decompressed);

      if ((block.size != decompressed.size) ||
          (memcmp(block.addr, decompressed.addr, block.size) != 0)) {
        fprintf(stderr, "Compression failed.\n");
      } else {
        printf("Compression was successfull.\n");
      }

      free(decompressed.addr);
      free(lz_data.addr);
      free(block.addr);
    }
  }
  return 0;
}