#include <LightGBM/metric.h>

#include <LightGBM/utils/log.h>

#include <cmath>

#include <vector>
#include <algorithm>

namespace LightGBM {

/*! \brief Declaration for some static members */
std::vector<double> DCGCalculator::label_gain_;
std::vector<double> DCGCalculator::discount_;
const data_size_t DCGCalculator::kMaxPosition = 10000;


void DCGCalculator::DefaultEvalAt(std::vector<int>* eval_at) {
  if (eval_at->empty()) {
    for (int i = 1; i <= 5; ++i) {
      eval_at->push_back(i);
    }
  } else {
    for (size_t i = 0; i < eval_at->size(); ++i) {
      CHECK(eval_at->at(i) > 0);
    }
  }
}

void DCGCalculator::DefaultLabelGain(std::vector<double>* label_gain) {
  if (!label_gain->empty()) { return; }
  // label_gain = 2^i - 1, may overflow, so we use 31 here
  const int max_label = 31;
  label_gain->push_back(0.0f);
  for (int i = 1; i < max_label; ++i) {
    label_gain->push_back(static_cast<double>((1 << i) - 1));
  }
}

void DCGCalculator::Init(const std::vector<double>& input_label_gain) {
  label_gain_.resize(input_label_gain.size());
  for (size_t i = 0; i < input_label_gain.size(); ++i) {
    label_gain_[i] = static_cast<double>(input_label_gain[i]);
  }
  discount_.resize(kMaxPosition);
  for (data_size_t i = 0; i < kMaxPosition; ++i) {
    discount_[i] = 1.0 / std::log2(2.0 + i);
  }
}

double DCGCalculator::CalMaxDCGAtK(data_size_t k, const label_t* label, data_size_t num_data) {
  double ret = 0.0f;
  // counts for all labels
  std::vector<data_size_t> label_cnt(label_gain_.size(), 0);
  for (data_size_t i = 0; i < num_data; ++i) {
    ++label_cnt[static_cast<int>(label[i])];
  }
  int top_label = static_cast<int>(label_gain_.size()) - 1;

  if (k > num_data) { k = num_data; }
  //  start from top label, and accumulate DCG
  for (data_size_t j = 0; j < k; ++j) {
    while (top_label > 0 && label_cnt[top_label] <= 0) {
      top_label -= 1;
    }
    if (top_label < 0) {
      break;
    }
    ret += discount_[j] * label_gain_[top_label];
    label_cnt[top_label] -= 1;
  }
  return ret;
}

void DCGCalculator::CalMaxDCG(const std::vector<data_size_t>& ks,
                              const label_t* label,
                              data_size_t num_data,
                              std::vector<double>* out) {
  std::vector<data_size_t> label_cnt(label_gain_.size(), 0);
  // counts for all labels
  for (data_size_t i = 0; i < num_data; ++i) {
    ++label_cnt[static_cast<int>(label[i])];
  }
  double cur_result = 0.0f;
  data_size_t cur_left = 0;
  int top_label = static_cast<int>(label_gain_.size()) - 1;
  // calculate k Max DCG by one pass
  for (size_t i = 0; i < ks.size(); ++i) {
    data_size_t cur_k = ks[i];
    if (cur_k > num_data) { cur_k = num_data; }
    for (data_size_t j = cur_left; j < cur_k; ++j) {
      while (top_label > 0 && label_cnt[top_label] <= 0) {
        top_label -= 1;
      }
      if (top_label < 0) {
        break;
      }
      cur_result += discount_[j] * label_gain_[top_label];
      label_cnt[top_label] -= 1;
    }
    (*out)[i] = cur_result;
    cur_left = cur_k;
  }
}


double DCGCalculator::CalDCGAtK(data_size_t k, const label_t* label,
                                const double* score, data_size_t num_data) {
  // get sorted indices by score
  std::vector<data_size_t> sorted_idx(num_data);
  for (data_size_t i = 0; i < num_data; ++i) {
    sorted_idx[i] = i;
  }
  std::stable_sort(sorted_idx.begin(), sorted_idx.end(),
                   [score](data_size_t a, data_size_t b) {return score[a] > score[b]; });

  if (k > num_data) { k = num_data; }
  double dcg = 0.0f;
  // calculate dcg
  for (data_size_t i = 0; i < k; ++i) {
    data_size_t idx = sorted_idx[i];
    dcg += label_gain_[static_cast<int>(label[idx])] * discount_[i];
  }
  return dcg;
}

void DCGCalculator::CalDCG(const std::vector<data_size_t>& ks, const label_t* label,
                           const double * score, data_size_t num_data, std::vector<double>* out) {
  // get sorted indices by score
  std::vector<data_size_t> sorted_idx(num_data);
  for (data_size_t i = 0; i < num_data; ++i) {
    sorted_idx[i] = i;
  }
  std::stable_sort(sorted_idx.begin(), sorted_idx.end(),
                   [score](data_size_t a, data_size_t b) {return score[a] > score[b]; });

  double cur_result = 0.0f;
  data_size_t cur_left = 0;
  // calculate multi dcg by one pass
  for (size_t i = 0; i < ks.size(); ++i) {
    data_size_t cur_k = ks[i];
    if (cur_k > num_data) { cur_k = num_data; }
    for (data_size_t j = cur_left; j < cur_k; ++j) {
      data_size_t idx = sorted_idx[j];
      cur_result += label_gain_[static_cast<int>(label[idx])] * discount_[j];
    }
    (*out)[i] = cur_result;
    cur_left = cur_k;
  }
}

void DCGCalculator::CheckLabel(const label_t* label, data_size_t num_data) {
  for (data_size_t i = 0; i < num_data; ++i) {
    label_t delta = std::fabs(label[i] - static_cast<int>(label[i]));
    if (delta > kEpsilon) {
      Log::Fatal("label should be int type (met %f) for ranking task,\n"
                 "for the gain of label, please set the label_gain parameter", label[i]);
    }
    if (static_cast<size_t>(label[i]) >= label_gain_.size() || label[i] < 0) {
      Log::Fatal("label (%d) excel the max range %d", label[i], label_gain_.size());
    }
  }
}

/*! \brief Declaration for some static members */
std::vector<double> MRRCalculator::label_gain_;
std::vector<double> MRRCalculator::discount_;
const data_size_t MRRCalculator::kMaxPosition = 10000;


void MRRCalculator::DefaultEvalAt(std::vector<int>* eval_at) {
  if (eval_at->empty()) {
    for (int i = 1; i <= 5; ++i) {
      eval_at->push_back(i);
    }
  } else {
    for (size_t i = 0; i < eval_at->size(); ++i) {
      CHECK(eval_at->at(i) > 0);
    }
  }
}

void MRRCalculator::DefaultLabelGain(std::vector<double>* label_gain) {
  if (!label_gain->empty()) { return; }
  // label_gain = 2^i - 1, may overflow, so we use 31 here
  const int max_label = 31;
  label_gain->push_back(0.0f);
  for (int i = 1; i < max_label; ++i) {
    label_gain->push_back(static_cast<double>((1 << i) - 1));
  }
}

void MRRCalculator::Init(const std::vector<double>& input_label_gain) {
  label_gain_.resize(input_label_gain.size());
  for (size_t i = 0; i < input_label_gain.size(); ++i) {
    label_gain_[i] = static_cast<double>(input_label_gain[i]);
  }
  discount_.resize(kMaxPosition);
  for (data_size_t i = 0; i < kMaxPosition; ++i) {
    discount_[i] = 1.0 / (1 + i);
  }
}

double MRRCalculator::CalMaxDCGAtK(data_size_t k, const label_t* label, data_size_t num_data) {
  double ret = 0.0f;
  // counts for all labels
  std::vector<data_size_t> label_cnt(label_gain_.size(), 0);
  for (data_size_t i = 0; i < num_data; ++i) {
    ++label_cnt[static_cast<int>(label[i])];
  }
  int top_label = static_cast<int>(label_gain_.size()) - 1;

  if (k > num_data) { k = num_data; }
  //  start from top label, and accumulate DCG
  for (data_size_t j = 0; j < k; ++j) {
    while (top_label > 0 && label_cnt[top_label] <= 0) {
      top_label -= 1;
    }
    if (top_label < 0) {
      break;
    }
    ret += discount_[j] * label_gain_[top_label];
    label_cnt[top_label] -= 1;
  }
  return ret;
}

void MRRCalculator::CalMaxDCG(const std::vector<data_size_t>& ks,
                              const label_t* label,
                              data_size_t num_data,
                              std::vector<double>* out) {
  std::vector<data_size_t> label_cnt(label_gain_.size(), 0);
  // counts for all labels
  for (data_size_t i = 0; i < num_data; ++i) {
    ++label_cnt[static_cast<int>(label[i])];
  }
  double cur_result = 0.0f;
  data_size_t cur_left = 0;
  int top_label = static_cast<int>(label_gain_.size()) - 1;
  // calculate k Max DCG by one pass
  for (size_t i = 0; i < ks.size(); ++i) {
    data_size_t cur_k = ks[i];
    if (cur_k > num_data) { cur_k = num_data; }
    for (data_size_t j = cur_left; j < cur_k; ++j) {
      while (top_label > 0 && label_cnt[top_label] <= 0) {
        top_label -= 1;
      }
      if (top_label < 0) {
        break;
      }
      cur_result += discount_[j] * label_gain_[top_label];
      label_cnt[top_label] -= 1;
    }
    (*out)[i] = cur_result;
    cur_left = cur_k;
  }
}


double MRRCalculator::CalDCGAtK(data_size_t k, const label_t* label,
                                const double* score, data_size_t num_data) {
  // get sorted indices by score
  std::vector<data_size_t> sorted_idx(num_data);
  for (data_size_t i = 0; i < num_data; ++i) {
    sorted_idx[i] = i;
  }
  std::stable_sort(sorted_idx.begin(), sorted_idx.end(),
                   [score](data_size_t a, data_size_t b) {return score[a] > score[b]; });

  if (k > num_data) { k = num_data; }
  double dcg = 0.0f;
  // calculate dcg
  for (data_size_t i = 0; i < k; ++i) {
    data_size_t idx = sorted_idx[i];
    dcg += label_gain_[static_cast<int>(label[idx])] * discount_[i];
  }
  return dcg;
}

void MRRCalculator::CalDCG(const std::vector<data_size_t>& ks, const label_t* label,
                           const double * score, data_size_t num_data, std::vector<double>* out) {
  // get sorted indices by score
  std::vector<data_size_t> sorted_idx(num_data);
  for (data_size_t i = 0; i < num_data; ++i) {
    sorted_idx[i] = i;
  }
  std::stable_sort(sorted_idx.begin(), sorted_idx.end(),
                   [score](data_size_t a, data_size_t b) {return score[a] > score[b]; });

  double cur_result = 0.0f;
  data_size_t cur_left = 0;
  // calculate multi dcg by one pass
  for (size_t i = 0; i < ks.size(); ++i) {
    data_size_t cur_k = ks[i];
    if (cur_k > num_data) { cur_k = num_data; }
    for (data_size_t j = cur_left; j < cur_k; ++j) {
      data_size_t idx = sorted_idx[j];
      cur_result += label_gain_[static_cast<int>(label[idx])] * discount_[j];
    }
    (*out)[i] = cur_result;
    cur_left = cur_k;
  }
}

void MRRCalculator::CheckLabel(const label_t* label, data_size_t num_data) {
  for (data_size_t i = 0; i < num_data; ++i) {
    label_t delta = std::fabs(label[i] - static_cast<int>(label[i]));
    if (delta > kEpsilon) {
      Log::Fatal("label should be int type (met %f) for ranking task,\n"
                 "for the gain of label, please set the label_gain parameter", label[i]);
    }
    if (static_cast<size_t>(label[i]) >= label_gain_.size() || label[i] < 0) {
      Log::Fatal("label (%d) excel the max range %d", label[i], label_gain_.size());
    }
  }
}


}  // namespace LightGBM
