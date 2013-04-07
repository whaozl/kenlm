#ifndef LM_SEARCH_TRIE__
#define LM_SEARCH_TRIE__

#include "lm/config.hh"
#include "lm/model_type.hh"
#include "lm/return.hh"
#include "lm/trie.hh"
#include "lm/weights.hh"

#include "util/file.hh"
#include "util/file_piece.hh"

#include <vector>

#include <assert.h>

namespace lm {

namespace builder { class Binarize; }

namespace ngram {
struct Backing;
class SortedVocabulary;
namespace trie {

template <class Quant, class Bhiksha> class TrieSearch;
class SortedFiles;
template <class Quant, class Bhiksha> void BuildTrie(SortedFiles &files, std::vector<uint64_t> &counts, const Config &config, TrieSearch<Quant, Bhiksha> &out, Quant &quant, const SortedVocabulary &vocab, Backing &backing);

template <class Quant, class Bhiksha> class TrieSearch {
  public:
    typedef NodeRange Node;
    typedef Quant Quantizer;

    typedef ::lm::ngram::trie::UnigramPointer UnigramPointer;
    typedef typename Quant::MiddlePointer MiddlePointer;
    typedef typename Quant::LongestPointer LongestPointer;

    static const bool kDifferentRest = false;

    static const ModelType kModelType = static_cast<ModelType>(TRIE_SORTED + Quant::kModelTypeAdd + Bhiksha::kModelTypeAdd);

    static const unsigned int kVersion = 1;

    static void UpdateConfigFromBinary(int fd, const std::vector<uint64_t> &counts, Config &config) {
      Quant::UpdateConfigFromBinary(fd, counts, config);
      util::AdvanceOrThrow(fd, Quant::Size(counts.size(), config) + Unigram::Size(counts[0]));
      Bhiksha::UpdateConfigFromBinary(fd, config);
    }

    static uint64_t Size(const std::vector<uint64_t> &counts, const Config &config) {
      uint64_t ret = Quant::Size(counts.size(), config) + Unigram::Size(counts[0]);
      for (unsigned char i = 1; i < counts.size() - 1; ++i) {
        ret += Middle::Size(Quant::MiddleBits(config), counts[i], counts[0], counts[i+1], config);
      }
      return ret + Longest::Size(Quant::LongestBits(config), counts.back(), counts[0]);
    }

    TrieSearch() : middle_begin_(NULL), middle_end_(NULL) {}

    ~TrieSearch() { FreeMiddles(); }

    void SetupMemory(uint8_t *mem, const std::vector<uint64_t> &counts, const Config &config) {
      SetupMemory(util::Rolling(mem), counts, config);
    }

    void SetupMemory(util::Rolling mem, const std::vector<uint64_t> &counts, const Config &config);

    void LoadedBinary();

    void InitializeFromARPA(const char *file, util::FilePiece &f, std::vector<uint64_t> &counts, const Config &config, SortedVocabulary &vocab, Backing &backing);

    unsigned char Order() const {
      return middle_end_ - middle_begin_ + 2;
    }

    ProbBackoff &UnknownUnigram() { return unigram_.Unknown(); }

    UnigramPointer LookupUnigram(WordIndex word, Node &next, bool &independent_left, uint64_t &extend_left) const {
      extend_left = static_cast<uint64_t>(word);
      UnigramPointer ret(unigram_.Find(word, next));
      independent_left = (next.begin == next.end);
      return ret;
    }

    MiddlePointer Unpack(uint64_t extend_pointer, unsigned char extend_length, Node &node) const {
      return MiddlePointer(quant_, extend_length - 2, middle_begin_[extend_length - 2].ReadEntry(extend_pointer, node));
    }

    MiddlePointer LookupMiddle(unsigned char order_minus_2, WordIndex word, Node &node, bool &independent_left, uint64_t &extend_left) const {
      util::BitAddress address(middle_begin_[order_minus_2].Find(word, node, extend_left));
      independent_left = (address.base == NULL) || (node.begin == node.end);
      return MiddlePointer(quant_, order_minus_2, address);
    }

    LongestPointer LookupLongest(WordIndex word, const Node &node) const {
      return LongestPointer(quant_, longest_.Find(word, node));
    }

    bool FastMakeNode(const WordIndex *begin, const WordIndex *end, Node &node) const {
      assert(begin != end);
      bool independent_left;
      uint64_t ignored;
      LookupUnigram(*begin, node, independent_left, ignored);
      for (const WordIndex *i = begin + 1; i < end; ++i) {
        if (independent_left || !LookupMiddle(i - begin - 1, *i, node, independent_left, ignored).Found()) return false;
      }
      return true;
    }

    // For building directly from text.
    void ExternalInsert(unsigned int order, WordIndex last_word, const ProbBackoff &payload);
    void ExternalFinished(const Config &config, WordIndex unigram_count_inc_unk);

  private:
    friend void BuildTrie<Quant, Bhiksha>(SortedFiles &files, std::vector<uint64_t> &counts, const Config &config, TrieSearch<Quant, Bhiksha> &out, Quant &quant, const SortedVocabulary &vocab, Backing &backing);
    friend class lm::builder::Binarize;

    // Middles are managed manually so we can delay construction and they don't have to be copyable.  
    void FreeMiddles() {
      for (const Middle *i = middle_begin_; i != middle_end_; ++i) {
        i->~Middle();
      }
      free(middle_begin_);
    }

    typedef trie::BitPackedMiddle<Bhiksha> Middle;
    typedef trie::BitPackedLongest Longest;
    typedef ::lm::ngram::trie::Unigram Unigram;

    // Used to keep the fixed mappings if SetupMemory was provided with a rolling map.
    util::scoped_memory quant_backing_;

    Longest longest_;

    Middle *middle_begin_, *middle_end_;
    Quant quant_;

    Unigram unigram_;
};

} // namespace trie
} // namespace ngram
} // namespace lm

#endif // LM_SEARCH_TRIE__
