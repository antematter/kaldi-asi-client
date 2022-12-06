#include "asr_client_imp.h"
#include "feat/wave-reader.h"
#include <fstream>
#include <iostream>
#include <istream>
#include <random>
#include <streambuf>
#include <string>
#include <unistd.h>
#include <vector>

constexpr auto URL = "localhost:8001";
constexpr auto MODEL = "kaldi_online";

enum {
  CHUNK_LENGTH = 8160,
  NCLIENTS = 10,
};

struct client {
  TritonASRClient asr_client;
  std::vector<kaldi::WaveData> inputs;
  std::vector<std::string> outputs;
};

struct membuf : std::streambuf {
  membuf(char *begin, char *end) { this->setg(begin, begin, end); }
};

int feed_wav(TritonASRClient &asr_client, kaldi::WaveData &wave_data) {
  uint64_t index = 0;
  int32 offset = 0;

  while (true) {
    kaldi::SubVector<kaldi::BaseFloat> data(wave_data.Data(), 0);

    int32 samp_remaining = data.Dim() - offset;
    int32 num_samp = std::min(static_cast<int32>(CHUNK_LENGTH), samp_remaining);

    bool is_last_chunk = (CHUNK_LENGTH >= samp_remaining);
    bool is_first_chunk = (offset == 0);

    kaldi::SubVector<kaldi::BaseFloat> wave_part(data, offset, num_samp);

    std::cerr << "Sending chunk " << index << " with " << num_samp << " samples"
              << '\n'
              << "is_first: " << is_first_chunk
              << ", is_last: " << is_last_chunk << '\n';

    asr_client.SendChunk(1, is_first_chunk, is_last_chunk, wave_part.Data(),
                         wave_part.SizeInBytes(), index++);

    offset += num_samp;

    if (is_last_chunk) {
      break;
    }
  }

  return 0;
}

int feed_wav_from_charbuf(TritonASRClient &asr_client, char *buf, size_t len) {
  membuf sbuf(buf, buf + len);
  std::istream is(&sbuf);

  kaldi::WaveData wave_data;
  wave_data.Read(is);

  return feed_wav(asr_client, wave_data);
}

int main(int argc, char *const argv[]) {
  if (argc < 2) {
    std::cerr << "Usage: " << argv[0] << " FILE" << '\n';
    return 1;
  }

  std::ifstream istrm(argv[1], std::ios::binary);
  kaldi::WaveData wave_data;

  wave_data.Read(istrm);

  float samp_freq = wave_data.SampFreq();
  double duration = wave_data.Duration();

  std::cout << "Loaded file with frequency " << samp_freq << "hz, duration "
            << duration << '\n';

  TritonASRClient asr_client(URL, MODEL, NCLIENTS, true, false, false,
                             samp_freq);

  feed_wav(asr_client, wave_data);

  asr_client.WaitForCallbacks();
}
