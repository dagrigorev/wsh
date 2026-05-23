#include "ngram_model.h"
#include <cstring>
#include <cmath>
#include <algorithm>

namespace wsh {

NgramModel::NgramModel()
    : data_(nullptr), data_size_(0), order_(0),
      num_entries_(0), total_count_(0) {}

void NgramModel::Init(const unsigned char* data, int data_size,
                      int order, int num_entries, int total_count) {
    data_ = data;
    data_size_ = data_size;
    order_ = order;
    num_entries_ = num_entries;
    total_count_ = total_count;
}

uint32_t NgramModel::ReadU32(const unsigned char* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

uint16_t NgramModel::ReadU16(const unsigned char* p) {
    return ((uint16_t)p[0] << 8) | (uint16_t)p[1];
}

uint32_t NgramModel::MakeNgram(const char* s) {
    uint32_t v = 0;
    for (int i = 0; i < 4; i++) {
        v = (v << 8) | (unsigned char)(s[i] ? s[i] : ' ');
    }
    return v;
}

uint32_t NgramModel::ContextOf(uint32_t ngram) {
    return (ngram >> 8) & 0xFFFFFF;
}

const NgramModel::Entry* NgramModel::FindEntry(uint32_t ngram) const {
    if (!data_ || num_entries_ <= 0) return nullptr;
    const int entry_size = 6;
    int lo = 0, hi = num_entries_ - 1;
    while (lo <= hi) {
        int mid = lo + (hi - lo) / 2;
        uint32_t val = ReadU32(data_ + mid * entry_size);
        if (val == ngram)
            return reinterpret_cast<const Entry*>(data_ + mid * entry_size);
        else if (val < ngram)
            lo = mid + 1;
        else
            hi = mid - 1;
    }
    return nullptr;
}

double NgramModel::LogProbability(const std::string& str) const {
    if (!data_ || str.empty()) return 0.0;
    const int entry_size = 6;
    double log_prob = 0.0;
    for (size_t i = 0; i + (size_t)order_ <= str.size(); i++) {
        uint32_t ng = MakeNgram(str.c_str() + i);
        const Entry* e = FindEntry(ng);
        double prob = (e ? (double)ReadU16((const unsigned char*)&e->count)
                        : 0.1) / total_count_;
        log_prob += std::log(prob);
    }
    return log_prob;
}

char NgramModel::PredictNext(const char* context3) const {
    if (!data_ || order_ < 2) return 0;
    const int entry_size = 6;
    uint32_t target_ctx = MakeNgram(context3) >> 8;
    int lo = 0, hi = num_entries_ - 1;
    int first = -1;
    while (lo <= hi) {
        int mid = lo + (hi - lo) / 2;
        uint32_t ctx = ReadU32(data_ + mid * entry_size) >> 8;
        if (ctx < target_ctx)
            lo = mid + 1;
        else {
            if (ctx == target_ctx) first = mid;
            hi = mid - 1;
        }
    }
    if (first < 0) return 0;
    int best_count = 0;
    char best_char = 0;
    for (int i = first; i < num_entries_; i++) {
        uint32_t ng = ReadU32(data_ + i * entry_size);
        if (ContextOf(ng) != target_ctx) break;
        uint16_t cnt = ReadU16(data_ + i * entry_size + 4);
        if (cnt > best_count) {
            best_count = cnt;
            best_char = (char)(ng & 0xFF);
        }
    }
    return best_char;
}

std::vector<std::pair<std::string, double>> NgramModel::RankCandidates(
    const std::vector<std::string>& candidates,
    const std::string& original, double alpha) const {
    std::vector<std::pair<std::string, double>> scored;
    int orig_len = (int)original.size();
    for (const auto& cand : candidates) {
        double lp = LogProbability(cand);
        int dist = 0;
        int max_len = std::max(orig_len, (int)cand.size());
        int** d = new int*[orig_len + 1];
        for (int i = 0; i <= orig_len; i++) {
            d[i] = new int[max_len + 1];
            d[i][0] = i;
        }
        for (int j = 0; j <= (int)cand.size(); j++) d[0][j] = j;
        for (int i = 1; i <= orig_len; i++) {
            for (int j = 1; j <= (int)cand.size(); j++) {
                int cost = (original[i-1] == cand[j-1]) ? 0 : 1;
                d[i][j] = std::min({ d[i-1][j] + 1, d[i][j-1] + 1, d[i-1][j-1] + cost });
                if (i > 1 && j > 1 && original[i-1] == cand[j-2] && original[i-2] == cand[j-1])
                    d[i][j] = std::min(d[i][j], d[i-2][j-2] + cost);
            }
        }
        dist = d[orig_len][(int)cand.size()];
        for (int i = 0; i <= orig_len; i++) delete[] d[i];
        delete[] d;
        double score = lp - alpha * dist;
        scored.push_back({cand, score});
    }
    std::sort(scored.begin(), scored.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });
    return scored;
}

}
