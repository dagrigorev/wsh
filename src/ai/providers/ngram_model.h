#ifndef WSH_NGRAM_MODEL_H
#define WSH_NGRAM_MODEL_H

#include <cstdint>
#include <string>
#include <vector>
#include <utility>

namespace wsh {

class NgramModel {
public:
    NgramModel();

    void Init(const unsigned char* data, int data_size,
              int order, int num_entries, int total_count);

    double LogProbability(const std::string& str) const;

    char PredictNext(const char* context3) const;

    std::vector<std::pair<std::string, double>> RankCandidates(
        const std::vector<std::string>& candidates,
        const std::string& original, double alpha = 2.0) const;

    bool IsInitialized() const { return data_ != nullptr; }

private:
    struct Entry {
        uint32_t ngram;
        uint16_t count;
    };

    static uint32_t ReadU32(const unsigned char* p);
    static uint16_t ReadU16(const unsigned char* p);
    static uint32_t MakeNgram(const char* s);
    static uint32_t ContextOf(uint32_t ngram);

    const Entry* FindEntry(uint32_t ngram) const;

    const unsigned char* data_;
    int data_size_;
    int order_;
    int num_entries_;
    int total_count_;
};

}

#endif
