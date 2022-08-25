#ifndef TFM_INDEX_HPP
#define TFM_INDEX_HPP

#include <assert.h>

#include <sdsl/bit_vectors.hpp>
#include <sdsl/construct_lcp_helper.hpp>
#include <sdsl/csa_wt.hpp>
#include <sdsl/int_vector.hpp>
#include <sdsl/int_vector_buffer.hpp>
#include <sdsl/io.hpp>
#include <sdsl/rank_support.hpp>
#include <sdsl/sdsl_concepts.hpp>
#include <sdsl/select_support.hpp>
#include <sdsl/util.hpp>
#include <sdsl/wavelet_trees.hpp>

#include <algorithm>
#include <iostream>
#include <limits>
#include <string>
#include <utility>

#include "dbg_algorithms.hpp"

typedef uint32_t sa_index_t;

struct tfm_index_tag {};

//! a class representing a tunneled fm-index
template <
    class t_wt_type = sdsl::wt_blcd_int<>, // wt_int and wt_blcd_int can be used
                                           // interchangeably
    class t_bv_type = typename t_wt_type::bit_vector_type,
    class t_rank_type = typename t_bv_type::rank_1_type,
    class t_select_type = typename t_bv_type::select_1_type>
class tfm_index {
  public:
    typedef tfm_index_tag index_category;
    typedef sdsl::byte_alphabet_tag alphabet_category;
    typedef sdsl::int_vector<>::size_type size_type;

    typedef sdsl::int_vector<> text_type;
    typedef typename t_wt_type::value_type value_type;

    typedef t_wt_type wt_type;
    typedef t_bv_type bit_vector_type;
    typedef t_rank_type rank_type;
    typedef t_select_type select_type;

    // first index is next outgoing edge, second index is tunnel entry offset
    typedef std::pair<size_type, size_type> nav_type;

  private:
    template <typename t_tfm_index_type>
    friend void construct_tfm_index(
        t_tfm_index_type &tfm_index, uint64_t text_len,
        sdsl::int_vector_buffer<> &&L_buf, sdsl::bit_vector &&dout,
        sdsl::bit_vector &&din
    );

    // constructor for gSACAK
    template <typename t_tfm_index_type>
    friend void construct_tfm_index(
        t_tfm_index_type &tfm_index, std::string filename, const size_t psize,
        sdsl::cache_config &config
    );

    // constructor for the .L, .din, .dout files
    template <typename t_tfm_index_type>
    friend void construct_from_pfwg(
        t_tfm_index_type &tfm_index, const std::string filename
    );

    size_type text_len; // original textlen
    wt_type m_L;
    std::vector<size_type> m_C;
    bit_vector_type m_dout;
    rank_type m_dout_rank;
    select_type m_dout_select;
    bit_vector_type m_din;
    rank_type m_din_rank;
    select_type m_din_select;

  public:
    const wt_type &L = m_L;
    const std::vector<size_type> &C = m_C;
    const bit_vector_type &dout = m_dout;
    const rank_type &dout_rank = m_dout_rank;
    const select_type &dout_select = m_dout_select;
    const bit_vector_type &din = m_din;
    const rank_type &din_rank = m_din_rank;
    const select_type &din_select = m_din_select;

    //! returns the size of the original string
    size_type size() const { return text_len; };

    //! returns the end, i.e. the position in L where the string ends
    nav_type end() const { return std::make_pair((size_type)0, (size_type)0); }

    nav_type our_end() const {
        auto end = std::make_pair((size_type)0, (size_type)0);
        for (size_type i = 1; i < text_len; i++) {
            backwardstep(end);
        }
        return end;
    }

    //! returns the character preceding the current position
    value_type preceding_char(const nav_type &pos) const {
        return L[pos.first];
    }

    //! Operation performs an backward step from current position.
    //! function sets posm to the new value and returns the result
    //! of preceding_char( pos ) before the backward step was performed
    value_type backwardstep(nav_type &pos) const {
        size_type &i = pos.first; // create references into position pair
        size_type &o = pos.second;

        // navigate to next entry
        auto is = L.inverse_select(i);
        auto c = is.second;
        i = C[c] + is.first;

        // check for the start of a tunnel
        auto din_rank_ip1 = din_rank(i + 1);
        if (din[i] == 0) {
            o = i -
                din_select(din_rank_ip1); // save offset to uppermost entry edge
        }
        // navigate to outedges of current node
        i = dout_select(din_rank_ip1);

        // check for end of a tunnel
        if (dout[i + 1] == 0) {
            i += o; // jump back offset
            o = 0;
        }
        return c;
    };

    //! serializes opbject
    size_type serialize(
        std::ostream &out, sdsl::structure_tree_node *v, std::string name
    ) const {

        sdsl::structure_tree_node *child = sdsl::structure_tree::add_child(
            v, name, sdsl::util::class_name(*this)
        );
        size_type written_bytes = 0;
        written_bytes += sdsl::write_member(text_len, out, child, "text_len");

        written_bytes += m_L.serialize(out, child, "L");
        written_bytes += sdsl::serialize(m_C, out, child, "C");

        written_bytes += m_dout.serialize(out, child, "dout");
        written_bytes += m_dout_rank.serialize(out, child, "dout_rank");
        written_bytes += m_dout_select.serialize(out, child, "dout_select");

        written_bytes += m_din.serialize(out, child, "din");
        written_bytes += m_din_rank.serialize(out, child, "din_rank");
        written_bytes += m_din_select.serialize(out, child, "din_select");

        sdsl::structure_tree::add_size(child, written_bytes);
        return written_bytes;
    };

    //! loads a serialized object
    void load(std::istream &in) {

        sdsl::read_member(text_len, in);

        m_L.load(in);
        sdsl::load(m_C, in);

        m_dout.load(in);
        m_dout_rank.load(in, &m_dout);
        m_dout_select.load(in, &m_dout);

        m_din.load(in);
        m_din_rank.load(in, &m_din);
        m_din_select.load(in, &m_din);
    };
};

//// SPECIAL CONSTRUCTION FOR TUNNELED FM INDEX ///////////////////////////////

template <class t_index>
void construct(
    t_index &idx, const std::string &file, sdsl::cache_config &config,
    uint8_t num_bytes, tfm_index_tag
) {
    // assert(num_bytes == 1); //only byte input is allowed
    //  lol it's not

    // create a normal fm index
    sdsl::csa_wt<sdsl::wt_blcd_int<>, 0xFFFFFFFF, 0xFFFFFFFF> csa;
    { construct(csa, file, config, num_bytes); }

    // run construction algorithm
    {
        auto event = sdsl::memory_monitor::event("construct tunneled fm index");
        construct_tfm_index(idx, std::move(csa), config);
    }
};

//! function constructs a tfm index using a compressed suffix array in form of a
//! BWT in a wavelet tree. note that the csa is erased during construction
//! function returns the result of the dbg_algorithms::find_min_dbg - function
template <class t_tfm_index_type>
void construct_tfm_index(
    t_tfm_index_type &tfm_index, const std::string filename, size_t psize,
    sdsl::cache_config &config
) {

    // construct a wavelet tree out of the BWT
    sdsl::int_vector_buffer<> L(filename, std::ios::in, psize, 32, true);
    sdsl::wt_blcd_int<> wt_L = sdsl::wt_blcd_int<>(L, psize);

    std::vector<uint64_t> C = std::vector<uint64_t>(wt_L.sigma + 1, 0);
    for (uint64_t i = 0; i < psize; i++)
        C[L[i] + 1] += 1;
    for (uint64_t i = 0; i < wt_L.sigma; i++)
        C[i + 1] += C[i];
    std::cout << "C array for sigma = " << wt_L.sigma
              << " and length=" << wt_L.size() << " created!" << std::endl;

    typedef typename t_tfm_index_type::size_type size_type;
    std::pair<size_type, size_type> dbg_res;

    // find minimal edge-reduced DBG and store kmer bounds in a bitvector B
    sdsl::bit_vector B;
    {
        auto event = sdsl::memory_monitor::event("FINDMINDBG");
        dbg_res = dbg_algorithms::find_min_dbg(wt_L, C, B, config);
    }
    std::cout << "Min dbg found!" << std::endl;

    // use bitvector to determine prefix intervals to be tunneled
    auto event = sdsl::memory_monitor::event("TFMINDEXCONSTRUCT");
    sdsl::bit_vector dout = B;
    sdsl::bit_vector din;
    std::swap(din, B);
    dbg_algorithms::mark_prefix_intervals(wt_L, C, dout, din);

    // create a buffer for newly constructed L
    std::string tmp_key = sdsl::util::to_string(sdsl::util::pid()) + "_" +
                          sdsl::util::to_string(sdsl::util::id());
    std::string tmp_file_name = sdsl::cache_file_name(tmp_key, config);
    {
        sdsl::int_vector_buffer<> L_buf(tmp_file_name, std::ios::out);

        // remove redundant entries from L, dout and din
        size_type p = 0;
        size_type q = 0;
        for (size_type i = 0; i < wt_L.size(); i++) {
            if (din[i] == 1) {
                L_buf.push_back(wt_L[i]);
                dout[p++] = dout[i];
            }
            if (dout[i] == 1) {
                din[q++] = din[i];
            }
        }
        dout[p++] = 1;
        din[q++] = 1;
        dout.resize(p);
        din.resize(q);

        construct_tfm_index(
            tfm_index, psize, std::move(L_buf), std::move(dout), std::move(din)
        );
    }
    // remove buffer for L
    sdsl::remove(tmp_file_name);
}

void symbol_frequencies(
    std::vector<uint64_t> &C, sdsl::int_vector_buffer<> &L, uint64_t sigma
) {
    C = std::vector<uint64_t>(sigma + 1, 0);
    for (uint64_t i = 0; i < L.size(); i++)
        C[L[i] + 1] += 1;
    for (uint64_t i = 0; i < sigma; i++)
        C[i + 1] += C[i];
}

void symbol_frequencies(std::vector<uint64_t> &C, sdsl::int_vector<8> &L) {
    C = std::vector<uint64_t>(255, 0); // lol I hope it's enough :D
    for (uint64_t i = 0; i < L.size(); i++)
        C[L[i] + 1] += 1;
    for (uint64_t i = 0; i < C.size() - 1; i++) {
        C[i + 1] += C[i];
    }
}

template <class t_tfm_index_type>
void construct_tfm_index(
    t_tfm_index_type &tfm_index, uint64_t text_len,
    sdsl::int_vector_buffer<> &&L_buf, sdsl::bit_vector &&dout,
    sdsl::bit_vector &&din
) {
    // set original string size
    tfm_index.text_len = text_len;

    // construct tfm index from L, din and dout
    typedef typename t_tfm_index_type::wt_type wt_type;
    typedef typename t_tfm_index_type::bit_vector_type bv_type;

    // wavelet tree of L
    tfm_index.m_L = wt_type(L_buf, L_buf.size());
    symbol_frequencies(tfm_index.m_C, L_buf, tfm_index.m_L.sigma);

    // dout
    tfm_index.m_dout = bv_type(std::move(dout));
    // sdsl::util::init_support(tfm_index.m_dout_rank, &tfm_index.m_dout);
    sdsl::util::init_support(tfm_index.m_dout_select, &tfm_index.m_dout);

    // din
    tfm_index.m_din = bv_type(std::move(din));
    sdsl::util::init_support(tfm_index.m_din_rank, &tfm_index.m_din);
    // sdsl::util::init_support(tfm_index.m_din_select, &tfm_index.m_din);
}

void load_bitvector(
    sdsl::int_vector<1> &B, const std::string filename, const uint64_t n
) {
    FILE *fin = fopen(filename.c_str(), "rb");
    uint64_t cnt = 0;
    uint8_t buffer = 0;
    for (uint64_t i = 0; i < (n + 7) / 8; i++) {
        int e = fread(&buffer, sizeof(uint8_t), 1, fin);
        if (e < 0)
            std::cout << "ERROR during bitvector loading!" << std::endl;
        // std::cout << (int) buffer << std::endl;
        for (int j = 0; j < 8; j++) {
            bool bit = 1 & (buffer >> (7 - j));
            B[cnt++] = bit;
            if (cnt == n) {
                fclose(fin);
                return;
            }
        }
    }
}

template <class t_tfm_index_type>
void construct_from_pfwg(
    t_tfm_index_type &tfm_index, const std::string basename
) {
    // find original string size
    sdsl::int_vector<8> original;
    load_vector_from_file(original, basename, 1);

    sdsl::int_vector<8> L;
    load_vector_from_file(L, basename + ".L", 1);
    uint64_t size = L.size();
    sdsl::int_vector<1> din, dout;
    din.resize(size + 1);
    dout.resize(size + 1);
    load_bitvector(
        din, basename + ".din", size + 1
    ); // one additional bit at the end
    load_bitvector(dout, basename + ".dout", size + 1);

    typedef ::tfm_index<>::wt_type wt_type;
    typedef ::tfm_index<>::bit_vector_type bv_type;

    tfm_index.text_len = original.size();
    sdsl::int_vector_buffer<> buf(basename + ".L", std::ios::in, size, 8, true);
    tfm_index.m_L = wt_type(buf, size);

    symbol_frequencies(tfm_index.m_C, L);
    tfm_index.m_dout = bv_type(std::move(dout));
    sdsl::util::init_support(tfm_index.m_dout_rank, &tfm_index.m_dout);
    sdsl::util::init_support(tfm_index.m_dout_select, &tfm_index.m_dout);

    tfm_index.m_din = bv_type(std::move(din));
    sdsl::util::init_support(tfm_index.m_din_rank, &tfm_index.m_din);
    sdsl::util::init_support(tfm_index.m_din_select, &tfm_index.m_din);
}

#endif
