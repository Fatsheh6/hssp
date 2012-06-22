//  Copyright Maarten L. Hekkelman, Radboud University 2011.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#include "mas.h"

#include <cmath>

#include <boost/algorithm/string.hpp>
#include <boost/filesystem/convenience.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/iostreams/filtering_streambuf.hpp>
#include <boost/iostreams/filtering_stream.hpp>
#include <boost/iostreams/copy.hpp>
#include <boost/iostreams/filter/bzip2.hpp>
#include <boost/iostreams/filter/gzip.hpp>
#include <boost/foreach.hpp>
#define foreach BOOST_FOREACH
#include <boost/date_time/gregorian/gregorian.hpp>
#include <boost/date_time/date_clock_device.hpp>
#include <boost/regex.hpp>
#include <boost/ptr_container/ptr_vector.hpp>

// our includes
#include "buffer.h"
#include "utils.h"

#if P_WIN
#pragma warning (disable: 4267)
#endif

using namespace std;
namespace ba = boost::algorithm;
namespace io = boost::iostreams;

// --------------------------------------------------------------------
// utility routine
	
inline bool is_gap(char aa)
{
	return kResidueIX[uint8(aa)] == -2;
	// == '-' or aa == '~' or aa == '.' or aa == '_' or aa == ' ';
}

// --------------------------------------------------------------------
// basic named sequence type and a multiple sequence alignment container

struct insertion
{
	uint32			m_ipos, m_jpos;
	string			m_seq;
};
	
class seq
{
  public:
				seq(const string& acc);
				~seq();
				
	seq&		operator=(const seq&);

	void		swap(seq& o);

	string		acc() const							{ return m_impl->m_acc; }

	void		id(const string& id);
	string		id() const							{ return m_impl->m_id; }

	void		pdb(const string& pdb);
	string		pdb() const							{ return m_impl->m_pdb; }
	
	void		desc(const string& desc);
	string		desc() const						{ return m_impl->m_desc; }
	
	void		hssp(const string& hssp);

	float		identity() const					{ return m_impl->m_identical; }
	float		similarity() const					{ return m_impl->m_similar; }

	uint32		ifir() const						{ return m_impl->m_ifir; }
	uint32		ilas() const						{ return m_impl->m_ilas; }
	uint32		jfir() const						{ return m_impl->m_jfir; }
	uint32		jlas() const						{ return m_impl->m_jlas; }
	uint32		gapn() const						{ return m_impl->m_gapn; }
	uint32		gaps() const						{ return m_impl->m_gaps; }
	
	uint32		alignment_length() const			{ return m_impl->m_length; }
	uint32		seqlen() const						{ return m_impl->m_seqlen; }
	
	const list<insertion>&
				insertions() const					{ return m_impl->m_insertions; }

	void		append(const string& seq);

	void		update(const seq& qseq);
	static void	update_all(buffer<seq*>& b, const seq& qseq);

	bool		operator<(const seq& o) const		{ return m_impl->m_score > o.m_impl->m_score or
														(m_impl->m_score == o.m_impl->m_score and length() > o.length()); }

	uint32		length() const						{ return m_impl->m_end - m_impl->m_begin; }

	char&		operator[](uint32 offset)
				{
					assert(offset < m_impl->m_size);
					return m_impl->m_seq[offset];
				}

	char		operator[](uint32 offset) const
				{
					assert(offset < m_impl->m_size);
					return m_impl->m_seq[offset];
				}

	template<class T>
	class basic_iterator : public std::iterator<bidirectional_iterator_tag,T>
	{
	  public:
		typedef typename std::iterator<std::bidirectional_iterator_tag, T>	base_type;
		typedef	typename base_type::reference								reference;
		typedef typename base_type::pointer									pointer;

						basic_iterator(T* s) : m_seq(s) {}
						basic_iterator(const basic_iterator& o) : m_seq(o.m_seq) {}

		basic_iterator&	operator=(const basic_iterator& o)
						{
							m_seq = o.m_seq;
							return *this;
						}

		reference		operator*()					{ return *m_seq; }
		reference		operator->()				{ return *m_seq; }

		basic_iterator&	operator++()				{ ++m_seq; return *this; }
		basic_iterator	operator++(int)				{ basic_iterator iter(*this); operator++(); return iter; }

		basic_iterator&	operator--()				{ --m_seq; return *this; }
		basic_iterator	operator--(int)				{ basic_iterator iter(*this); operator--(); return iter; }

		bool			operator==(const basic_iterator& o) const
													{ return m_seq == o.m_seq; }
		bool			operator!=(const basic_iterator& o) const
													{ return m_seq != o.m_seq; }
	
		template<class U>
		friend basic_iterator<U> operator-(basic_iterator<U>, int);

	  private:
		pointer			m_seq;
	};
	
	typedef basic_iterator<char>		iterator;
	typedef basic_iterator<const char>	const_iterator;
	
	iterator		begin()							{ return iterator(m_impl->m_seq); }
	iterator		end()							{ return iterator(m_impl->m_seq + m_impl->m_size); }

	const_iterator	begin() const					{ return const_iterator(m_impl->m_seq); }
	const_iterator	end() const						{ return const_iterator(m_impl->m_seq + m_impl->m_size); }

  private:

	struct seq_impl
	{
					seq_impl(const string& id, const string& desc);
					~seq_impl();

		void		update(const seq_impl& qseq);

		iterator	begin()							{ return iterator(m_seq); }
		iterator	end()							{ return iterator(m_seq + m_size); }
	
		const_iterator
					begin() const					{ return const_iterator(m_seq); }
		const_iterator
					end() const						{ return const_iterator(m_seq + m_size); }

		string		m_id, m_acc, m_desc;
		uint32		m_ifir, m_ilas, m_jfir, m_jlas, m_length;
		float		m_identical, m_similar;
		uint32		m_begin, m_end;
		uint32		m_gaps, m_gapn;
		list<insertion>
					m_insertions;
		char*		m_data;
		char*		m_seq;
		uint32		m_refcount;
		uint32		m_size, m_space;
	};

	seq_impl*	m_impl;
	
				seq();
};

template<class T>
inline seq::basic_iterator<T> operator-(seq::basic_iterator<T> i, int o)
{
	seq::basic_iterator<T> r(i);
	r.m_seq -= o;
	return r;
}

//typedef boost::ptr_vector<seq> mseq;
typedef vector<seq>				mseq;

const uint32 kBlockSize = 512;

seq::seq_impl::seq_impl(const string& acc)
	: m_acc(acc)
	, m_identical(0)
	, m_similar(0)
	, m_length(0)
	, m_score(0)
	, m_begin(0)
	, m_end(0)
	, m_gaps(0)
	, m_gapn(0)
	, m_data(nullptr)
	, m_seq(nullptr)
	, m_refcount(1)
	, m_size(0)
	, m_space(0)
{
	m_ifir = m_ilas = m_jfir = m_jlas = 0;
}

seq::seq_impl::~seq_impl()
{
	assert(m_refcount == 0);
	delete m_data;
}

seq::seq(const seq& s)
	: m_impl(s.m_impl)
{
	++m_impl->m_refcount;
}

seq& seq::operator=(const seq& rhs)
{
	if (this != &rhs)
	{
		if (--m_impl->m_refcount == 0)
			delete m_impl;
		
		m_impl = rhs.m_impl;
		
		++m_impl->m_refcount;
	}

	return *this;
}

seq::~seq()
{
	if (--m_impl->m_refcount == 0)
		delete m_impl;
}

void seq::swap(seq& o)
{
	std::swap(m_impl, o.m_impl);
}

void seq::id(const string& id)
{
	m_impl->m_id = id;
}

void seq::pdb(const string& pdb)
{
	m_impl->m_pdb = pdb;
}

void seq::desc(const string& desc)
{
	m_impl->m_desc = desc;
}

void seq::hssp(const string& hssp)
{
	// HSSP score=0.98/1.00 aligned=1-46/1-46 length=46 ngaps=0 gaplen=0 seqlen=46
	
	static const boost::regex
		re1("score=(\\d\\.\\d+)/(\\d\\.\\d+)"),
		re2("aligned=(\\d+)-(\\d+)/(\\d+)-(\\d+)"),
		re3("length=(\\d+)"),
		re4("ngaps=(\\d+)"),
		re5("gaplen=(\\d+)"),
		re6("seqlen=(\\d+)");
	
	boost::smatch m;
	if (boost::regex_search(hssp, m, re1))
	{
		m_impl->m_identical = boost::lexical_cast<float>(m[1]);
		m_impl->m_similar = boost::lexical_cast<float>(m[2]);
	}
	
	if (boost::regex_search(hssp, m, re2))
	{
		m_impl->m_ifir = boost::lexical_cast<uint32>(m[1]);
		m_impl->m_ilas = boost::lexical_cast<uint32>(m[2]);
		m_impl->m_jfir = boost::lexical_cast<uint32>(m[3]);
		m_impl->m_jlas = boost::lexical_cast<uint32>(m[4]);
	}

	if (boost::regex_search(hssp, m, re3))
		m_impl->m_length = boost::lexical_cast<uint32>(m[1]);
	
	if (boost::regex_search(hssp, m, re4))
		m_impl->m_ngaps = boost::lexical_cast<uint32>(m[1]);
	
	if (boost::regex_search(hssp, m, re5))
		m_impl->m_gapn = boost::lexical_cast<uint32>(m[1]);
	
	if (boost::regex_search(hssp, m, re6))
		m_impl->m_seqlen = boost::lexical_cast<uint32>(m[1]);
}

void seq::append(const string& seq)
{
	if (m_impl->m_size + seq.length() > m_impl->m_space)
	{
		// increase storage for the sequences
		uint32 k = m_impl->m_space;
		if (k == 0)
			k = kBlockSize;
		uint32 n = k * 2;
		if (n < seq.length())
			n = seq.length();
		char* p = new char[n];
		memcpy(p, m_impl->m_data, m_impl->m_size);
		delete [] m_impl->m_data;
		m_impl->m_data = m_impl->m_seq = p;
		m_impl->m_space = n;
	}

	memcpy(m_impl->m_seq + m_impl->m_size, seq.c_str(), seq.length());
	m_impl->m_end = m_impl->m_size += seq.length();
}

void seq::update_all(buffer<seq*>& b, const seq& qseq)
{
	for (;;)
	{
		seq* s = b.get();
		if (s == nullptr)
			break;

		s->update(qseq);
	}

	b.put(nullptr);
}

void seq::update(const seq& qseq)
{
	m_impl->update(*qseq.m_impl);
}

void seq::seq_impl::update(const seq_impl& qseq)
{
	uint32 ipos = 1, jpos = m_jfir;
	if (jpos == 0)
		jpos = 1;

	bool sgapf = false, qgapf = false;
	uint32 gapn = 0, gaps = 0;
	
	const_iterator qi = qseq.begin();
	iterator si = begin();
	uint32 i = 0;
	insertion ins = {};
	
	// reset statistics
	m_ifir = m_similar = m_identical = m_gapn = m_gaps = 0;
	m_begin = numeric_limits<uint32>::max();
	m_end = 0;
	
	uint32 length = 0;
	
	for (; qi != qseq.end(); ++qi, ++si, ++i)
	{
		bool qgap = is_gap(*qi);
		bool sgap = is_gap(*si);

		if (qgap and sgap)
			continue;

		// only update alignment length when we have started
		if (length > 0)
			++length;

		if (sgap)
		{
			if (not (sgapf or qgapf))
				++gaps;
			sgapf = true;
			++gapn;
			++ipos;

			continue;
		}
		else if (qgap)
		{
			if (not qgapf)
			{
				iterator gsi = si - 1;
				while (gsi != begin() and is_gap(*gsi))
					--gsi;
				
				ins.m_ipos = ipos;
				ins.m_jpos = jpos;
				ins.m_seq = *gsi = tolower(*gsi);
			}

			ins.m_seq += *si;
			
			if (not (sgapf or qgapf))
				++gaps;

			qgapf = true;
			++gapn;
			++jpos;
		}
		else
		{
			if (qgapf)
			{
				*si = tolower(*si);
				ins.m_seq += *si;
				m_insertions.push_back(ins);
			}
			
			sgapf = false;
			qgapf = false;

			m_ilas = ipos;
			if (m_ifir == 0)	// alignment didn't start yet
			{
				m_ifir = ipos;
				length = 1;
			}
			else
			{
				// no gaps in s or q, update gap counters and length
				m_gapn += gapn;
				m_gaps += gaps;
				m_length = length;
			}

			gaps = 0; // reset gap info
			gapn = 0;

			++ipos;
			++jpos;
		}

		if (*qi == *si)
			++m_identical;
		
		// validate the sequences while counting similarity
		uint8 rq = kResidueIX[static_cast<uint8>(*qi)];
		if (rq == -1)
			THROW(("Invalid letter in query sequence (%c)", *qi));
		uint8 rs = kResidueIX[static_cast<uint8>(*si)];
		if (rs == -1)
			THROW(("Invalid letter in query sequence (%c)", *si));
		
		if (rq >= 0 and rs >= 0 and kD(rq, rs) >= 0)
			++m_similar;

		if (m_begin == numeric_limits<uint32>::max())
			m_begin = i;
		
		m_end = i + 1;
	}
	
	if (m_begin == numeric_limits<uint32>::max())
		m_begin = m_end = 0;
	else
	{
		assert(m_begin <= m_size);
		assert(m_end <= m_size);

		for (i = 0; i < m_size; ++i)
		{
			if (i < m_begin or i >= m_end)
				m_seq[i] = ' ';
			else if (is_gap(m_seq[i]))
				m_seq[i] = '.';
		}
	}

	m_score = float(m_identical) / m_length;
}

namespace std
{
	template<>
	void swap(hmmer::seq& a, hmmer::seq& b)
	{
		a.swap(b);
	}
}

// --------------------------------------------------------------------
// ResidueHInfo is a class to store information about a residue in the
// original query sequence, along with statistics.

struct ResidueHInfo
{
			ResidueHInfo(const string& ri);

	string	m_ri, m_pr;
};

ResidueHInfo::ResidueHInfo(const string& ri)
	: m_ri(ri)
{
	for (int i = 6; i > 1; --i)
		m_ri[i] = m_ri[i - 1];
	m_ri[0] = ' ';
	
	for (int i = 40; i < 44; ++i)
		m_ri[i] = m_ri[i + 1];
	m_ri[45] = ' ';
}

typedef shared_ptr<ResidueHInfo>						res_ptr;
typedef vector<res_ptr>									res_list;
typedef boost::iterator_range<res_list::iterator>::type	res_range;

// --------------------------------------------------------------------

void ReadHSSP2File(istream& is, string& id, string& header, mseq& msa, res_list& residues, const string& q)
{
	string line, qseq, qr, qid;

	getline(is, line);
	if (line != "# STOCKHOLM 1.0")
		throw mas_exception("Not a stockholm file, missing first line");

	uint32 ix = 0, n = 0;
	string::size_type ccOffset = 0;
	
	map<string,uint32> index;
	
	for (;;)
	{
		line.clear();
		getline(is, line);
		
		if (line.empty())
		{
			if (not is.good())
				THROW(("Stockholm file is truncated or incomplete"));
			continue;
		}
		
		if (line == "//")
			break;

		if (ba::starts_with(line, "#=GF ID "))
		{
			qid = line.substr(8);
			index[qid] = msa.size();
			msa.push_back(seq(qid));
			continue;
		}

		if (ba::starts_with(line, "#=GF CC PDBID "))
		{
			id = line.substr(14);
			continue;
		}

		if (ba::starts_with(line, "#=GF CC DATE   ") or
			ba::starts_with(line, "#=GF CC PDBID  ") or
			ba::starts_with(line, "#=GF CC HEADER ") or
			ba::starts_with(line, "#=GF CC COMPND ") or
			ba::starts_with(line, "#=GF CC AUTHOR ") or
			ba::starts_with(line, "#=GF CC DBREF  "))
		{
			header += line.substr(15) + '\n';
			continue;
		}

		if (ba::starts_with(line, "#=RI "))
		{
			residues.push_back(new ResidueHInfo(line.substr(8)));
			continue;
		}
		
		if (ba::starts_with(line, "#=PR "))
		{
			uint32 nr = boost::lexical_cast<uint32>(ba::trim_copy(line.substr(8, 5))) - 1;
			if (nr >= residues.size())
				throw mas_exception("invalid input file");
			
			residues[nr]->m_pr = line.substr(13);
			continue;
		}
	
		if (ba::starts_with(line, "#=GS "))
		{
			line.erase(0, 5);
			if (msa.empty() and ba::starts_with(line, qid))	// first GS line, fetch the width
			{
				ccOffset = line.find("CC");
			}
			else
			{
				string id = line.substr(0, ccOffset);
				ba::trim(id);
				
				if (index.find(id) == index.end())
				{
					index.push_back(msa.size());
					msa.push_back(seq(id));
				}
				
				line.erase(0, ccOffset)
				
				if (ba::starts_with(line, "ID "))
					msa[index[id]].m_id = line.substr(3);
				else if (ba::starts_with(line, "DE "))
					msa[index[id]].m_de = line.substr(3);
				else if (ba::starts_with(line, "HSSP "))
					msa[index[id]].m_hssp = line.substr(5);
				else if (ba::starts_with(line, "PDB "))
					msa[index[id]].m_pdb = line.substr(4, 4);
			}
			continue;
		}
		
		if (line[0] != '#' and line.length() > ccOffset)
		{
			string id = line.substr(0, ccOffset);
			ba::trim(id);
			
			string sseq = line.substr(ccOffset);
			
			if (id == qid())
			{
				ix = 0;
				qseq = sseq;
				n += sseq.length();
				
				foreach (char r, qseq)
				{
					if (not is_gap(r))
						qr += r;
				}
			}
			else
			{
				++ix;
				if (ix >= msa.size() or id != msa[ix].m_id)
					throw mas_exception("Invalid input file");
			}

			msa[ix].append(sseq);
		}
	}
}

// --------------------------------------------------------------------
// Hit is a class to store hit information and all of its statistics.
	
struct Hit
{
					Hit(seq& s, seq& q, char chain, uint32 offset);
					~Hit();

	seq&			m_seq;
	seq&			m_qseq;
	char			m_chain;
	uint32			m_nr, m_ifir, m_ilas, m_offset;

	bool			operator<(const Hit& rhs) const
					{
						return m_ide > rhs.m_ide or
							(m_ide == rhs.m_ide and m_seq.alignment_length() > rhs.m_seq.alignment_length()) or
							(m_ide == rhs.m_ide and m_seq.alignment_length() == rhs.m_seq.alignment_length() and m_seq.id2() > rhs.m_seq.id2());
					}
};

typedef shared_ptr<Hit> hit_ptr;
typedef vector<hit_ptr>	hit_list;

// Create a Hit object based on a jackhmmer alignment pair
// first is the original query sequence, with gaps introduced.
// second is the hit sequence.
// Since this is jackhmmer output, we can safely assume the
// alignment does not contain gaps at the start or end of the query.
Hit::Hit(CDatabankPtr inDatabank, seq& s, seq& q, char chain, uint32 offset)
	: m_seq(s)
	, m_qseq(q)
	, m_chain(chain)
	, m_nr(0)
	, m_ifir(s.ifir() + offset)
	, m_ilas(s.ilas() + offset)
	, m_offset(offset)
{
	string id = m_seq.id2();
}

Hit::~Hit()
{
}

struct compare_hit
{
	bool operator()(hit_ptr a, hit_ptr b) const { return *a < *b; }
};

// --------------------------------------------------------------------
// Write collected information as a HSSP file to the output stream

void CreateHSSPOutput(
	const string&		inProteinID,
	const string&		inProteinDescription,
	float				inThreshold,
	uint32				inSeqLength,
	uint32				inNChain,
	uint32				inKChain,
	const string&		inUsedChains,
	hit_list&			hits,
	res_list&			res,
	ostream&			os)
{
	using namespace boost::gregorian;
	date today = day_clock::local_day();
	
	// print the header
	os << "HSSP       HOMOLOGY DERIVED SECONDARY STRUCTURE OF PROTEINS , VERSION 2.0 2011" << endl
	   << "PDBID      " << inProteinID << endl
	   << "DATE       file generated on " << to_iso_extended_string(today) << endl
	   << "SEQBASE    " << inDatabank->GetName() << " version " << inDatabank->GetVersion() << endl
	   << "THRESHOLD  according to: t(L)=(290.15 * L ** -0.562) + " << (inThreshold * 100) << endl
	   << "REFERENCE  Sander C., Schneider R. : Database of homology-derived protein structures. Proteins, 9:56-68 (1991)." << endl
	   << "CONTACT    Maintained at http://www.cmbi.ru.nl/ by Maarten L. Hekkelman <m.hekkelman@cmbi.ru.nl>" << endl
	   << inProteinDescription
	   << boost::format("SEQLENGTH %5.5d") % inSeqLength << endl
	   << boost::format("NCHAIN     %4.4d chain(s) in %s data set") % inNChain % inProteinID << endl;
	
	if (inKChain != inNChain)
		os << boost::format("KCHAIN     %4.4d chain(s) used here ; chains(s) : ") % inKChain << inUsedChains << endl;
	
	os << boost::format("NALIGN     %4.4d") % hits.size() << endl
	   << "NOTATION : ID: EMBL/SWISSPROT identifier of the aligned (homologous) protein" << endl
	   << "NOTATION : STRID: if the 3-D structure of the aligned protein is known, then STRID is the Protein Data Bank identifier as taken" << endl
	   << "NOTATION : from the database reference or DR-line of the EMBL/SWISSPROT entry" << endl
	   << "NOTATION : %IDE: percentage of residue identity of the alignment" << endl
	   << "NOTATION : %SIM (%WSIM):  (weighted) similarity of the alignment" << endl
	   << "NOTATION : IFIR/ILAS: first and last residue of the alignment in the test sequence" << endl
	   << "NOTATION : JFIR/JLAS: first and last residue of the alignment in the alignend protein" << endl
	   << "NOTATION : LALI: length of the alignment excluding insertions and deletions" << endl
	   << "NOTATION : NGAP: number of insertions and deletions in the alignment" << endl
	   << "NOTATION : LGAP: total length of all insertions and deletions" << endl
	   << "NOTATION : LSEQ2: length of the entire sequence of the aligned protein" << endl
	   << "NOTATION : ACCNUM: SwissProt accession number" << endl
	   << "NOTATION : PROTEIN: one-line description of aligned protein" << endl
	   << "NOTATION : SeqNo,PDBNo,AA,STRUCTURE,BP1,BP2,ACC: sequential and PDB residue numbers, amino acid (lower case = Cys), secondary" << endl
	   << "NOTATION : structure, bridge partners, solvent exposure as in DSSP (Kabsch and Sander, Biopolymers 22, 2577-2637(1983)" << endl
	   << "NOTATION : VAR: sequence variability on a scale of 0-100 as derived from the NALIGN alignments" << endl
	   << "NOTATION : pair of lower case characters (AvaK) in the alignend sequence bracket a point of insertion in this sequence" << endl
	   << "NOTATION : dots (....) in the alignend sequence indicate points of deletion in this sequence" << endl
	   << "NOTATION : SEQUENCE PROFILE: relative frequency of an amino acid type at each position. Asx and Glx are in their" << endl
	   << "NOTATION : acid/amide form in proportion to their database frequencies" << endl
	   << "NOTATION : NOCC: number of aligned sequences spanning this position (including the test sequence)" << endl
	   << "NOTATION : NDEL: number of sequences with a deletion in the test protein at this position" << endl
	   << "NOTATION : NINS: number of sequences with an insertion in the test protein at this position" << endl
	   << "NOTATION : ENTROPY: entropy measure of sequence variability at this position" << endl
	   << "NOTATION : RELENT: relative entropy, i.e.  entropy normalized to the range 0-100" << endl
	   << "NOTATION : WEIGHT: conservation weight" << endl
	   << endl
	   << "## PROTEINS : identifier and alignment statistics" << endl
	   << "  NR.    ID         STRID   %IDE %WSIM IFIR ILAS JFIR JLAS LALI NGAP LGAP LSEQ2 ACCNUM     PROTEIN" << endl;
	   
	// print the first list
	uint32 nr = 1;
	boost::format fmt1("%5.5d : %12.12s%4.4s    %4.2f  %4.2f%5.5d%5.5d%5.5d%5.5d%5.5d%5.5d%5.5d%5.5d  %10.10s %s");
	foreach (hit_ptr h, hits)
	{
		const seq& s(h->m_seq);

		string id = s.id();
		if (id.length() > 12)
			id.erase(12, string::npos);
		else if (id.length() < 12)
			id.append(12 - id.length(), ' ');
		
		string acc = s.acc();
		if (acc.length() > 10)
			acc.erase(10, string::npos);
		else if (acc.length() < 10)
			acc.append(10 - acc.length(), ' ');

		string pdb = s.pdb();
		if (pdb.empty())
			pdb.append(4, ' ');
		
		os << fmt1 % nr
				   % id % pdb
				   % h->m_ide % h->m_wsim % h->m_ifir % h->m_ilas % s.jfir() % s.jlas() % s.alignment_length()
				   % s.gaps() % s.gapn() % s.lseq()
				   % acc % s.desc()
		   << endl;
		
		++nr;
	}

	// print the alignments
	for (uint32 i = 0; i < hits.size(); i += 70)
	{
		uint32 n = i + 70;
		if (n > hits.size())
			n = hits.size();
		
		uint32 k[7] = {
			((i +  0) / 10 + 1) % 10,
			((i + 10) / 10 + 1) % 10,
			((i + 20) / 10 + 1) % 10,
			((i + 30) / 10 + 1) % 10,
			((i + 40) / 10 + 1) % 10,
			((i + 50) / 10 + 1) % 10,
			((i + 60) / 10 + 1) % 10
		};
		
		os << boost::format("## ALIGNMENTS %4.4d - %4.4d") % (i + 1) % n << endl
		   << boost::format(" SeqNo  PDBNo AA STRUCTURE BP1 BP2  ACC NOCC  VAR  ....:....%1.1d....:....%1.1d....:....%1.1d....:....%1.1d....:....%1.1d....:....%1.1d....:....%1.1d")
		   					% k[0] % k[1] % k[2] % k[3] % k[4] % k[5] % k[6] << endl;

		res_ptr last;
		foreach (res_ptr ri, res)
		{
			if (ri->letter == 0)
				os << boost::format(" %5.5d        !  !           0   0    0    0    0") % ri->seqNr << endl;
			else
			{
				string aln;
				
				foreach (hit_ptr hit, boost::make_iterator_range(hits.begin() + i, hits.begin() + n))
				{
					if (ri->seqNr >= hit->m_ifir and ri->seqNr <= hit->m_ilas)
						aln += hit->m_seq[ri->pos];
					else
						aln += ' ';
				}
				
				uint32 ivar = uint32(100 * (1 - ri->consweight));

				os << ' ' << boost::format("%5.5d%s%4.4d %4.4d  ") % ri->seqNr % ri->dssp % ri->nocc % ivar << aln << endl;
			}
		}
	}
	
	// ## SEQUENCE PROFILE AND ENTROPY
	os << "## SEQUENCE PROFILE AND ENTROPY" << endl
	   << " SeqNo PDBNo   V   L   I   M   F   W   Y   G   A   P   S   T   C   H   R   K   Q   E   N   D  NOCC NDEL NINS ENTROPY RELENT WEIGHT" << endl;
	
	res_ptr last;
	foreach (res_ptr r, res)
	{
		if (r->letter == 0)
		{
			os << boost::format("%5.5d          0   0   0   0   0   0   0   0   0   0   0   0   0   0   0   0   0   0   0   0     0    0    0   0.000      0  1.00")
				% r->seqNr << endl;
		}
		else
		{
			os << boost::format("%5.5d%5.5d %c") % r->seqNr % r->pdbNr % r->chain;

			for (uint32 i = 0; i < 20; ++i)
				os << boost::format("%4.4d") % r->dist[i];

			uint32 relent = uint32(100 * r->entropy / log(20.0));
			os << "  " << boost::format("%4.4d %4.4d %4.4d   %5.3f   %4.4d  %4.2f") % r->nocc % r->ndel % r->nins % r->entropy % relent % r->consweight << endl;
		}
	}
	
	// insertion list
	
	os << "## INSERTION LIST" << endl
	   << " AliNo  IPOS  JPOS   Len Sequence" << endl;

	foreach (hit_ptr h, hits)
	{
		//foreach (insertion& ins, h->insertions)
		foreach (const insertion& ins, h->m_seq.insertions())
		{
			string s = ins.m_seq;
			
			if (s.length() <= 100)
				os << boost::format(" %5.5d %5.5d %5.5d %5.5d ") % h->m_nr % (ins.m_ipos + h->m_offset) % ins.m_jpos % (ins.m_seq.length() - 2) << s << endl;
			else
			{
				os << boost::format(" %5.5d %5.5d %5.5d %5.5d ") % h->m_nr % (ins.m_ipos + h->m_offset) % ins.m_jpos % (ins.m_seq.length() - 2) << s.substr(0, 100) << endl;
				s.erase(0, 100);
				
				while (not s.empty())
				{
					uint32 n = s.length();
					if (n > 100)
						n = 100;
					
					os << "     +                   " << s.substr(0, n) << endl;
					s.erase(0, n);
				}
			}
		}			
	}
	
	os << "//" << endl;
}

// --------------------------------------------------------------------
// Convert a multiple sequence alignment as created by jackhmmer to 
// a set of information as used by HSSP.

void ChainToHits(mseq& msa, const MChain& chain, hit_list& hits, res_list& res)
{
	if (VERBOSE)
		cerr << "Creating hits...";
	
	hit_list nhits;

	for (uint32 i = 1; i < msa.size(); ++i)
	{
		uint32 docNr;
		
		if (not inDatabank->GetDocumentNr(msa[i].id2(), docNr))
		{
			if (VERBOSE)
				cerr << "Missing document " << msa[i].id2() << endl;
			continue;
		}

		hit_ptr h(new Hit(inDatabank, msa[i], msa[0], chain.GetChainID(), res.size()));
		nhits.push_back(h);
	}
	
	if (VERBOSE)
		cerr << " done" << endl
			 << "Continuing with " << nhits.size() << " hits" << endl
			 << "Calculating residue info...";

	const vector<MResidue*>& residues = chain.GetResidues();
	vector<MResidue*>::const_iterator ri = residues.begin();

	const seq& s = msa.front();
	for (uint32 i = 0; i < s.length(); ++i)
	{
		if (is_gap(s[i]))
			continue;

		assert(ri != residues.end());
		
		if (ri != residues.begin() and (*ri)->GetNumber() > (*(ri - 1))->GetNumber() + 1)
			res.push_back(res_ptr(new ResidueHInfo(res.size() + 1)));
		
		string dssp = ResidueToDSSPLine(**ri).substr(5, 34);

		res.push_back(res_ptr(new ResidueHInfo(s[i], i,
			chain.GetChainID(), res.size() + 1, (*ri)->GetNumber(), dssp)));

		++ri;
	}
	
	if (VERBOSE)
		cerr << " done" << endl;
	
//	assert(ri == residues.end());
	hits.insert(hits.end(), nhits.begin(), nhits.end());
}

void CreateHSSP(
	CDatabankPtr		inDatabank,
	MProtein&			inProtein,
	const fs::path&		inFastaDir,
	const fs::path&		inJackHmmer,
	uint32				inIterations,
	uint32				inMaxHits,
	uint32				inMinSeqLength,
	float				inCutOff,
	ostream&			outHSSP)
{
	// construct a set of unique sequences, containing only the largest ones in case of overlap
	vector<string> seqset;
	vector<uint32> ix;
	vector<const MChain*> chains;
	
	foreach (const MChain* chain, inProtein.GetChains())
	{
		string seq;
		chain->GetSequence(seq);
		
		if (seq.length() < inMinSeqLength)
			continue;
		
		chains.push_back(chain);
		seqset.push_back(seq);
		ix.push_back(ix.size());
	}
	
	if (seqset.empty())
		THROW(("Not enough sequences in PDB file of length %d", inMinSeqLength));

	if (seqset.size() > 1)
		ClusterSequences(seqset, ix);
	
	// only take the unique sequences
	ix.erase(unique(ix.begin(), ix.end()), ix.end());

	// now create a stockholmid array
	vector<string> stockholmIds;
	
	foreach (uint32 i, ix)
	{
		const MChain* chain = chains[i];
		
		stringstream s;
		s << chain->GetChainID() << '=' << inProtein.GetID() << '-' << stockholmIds.size();
		stockholmIds.push_back(s.str());
	}
	
	CreateHSSP(inDatabank, inProtein, fs::path(), inFastaDir, inJackHmmer, inIterations, inMaxHits, stockholmIds, inCutOff, outHSSP);
}

void CreateHSSP(
	CDatabankPtr		inDatabank,
	const string&		inProtein,
	const string&		inProteinID,
	const fs::path&		inDataDir,
	const fs::path&		inFastaDir,
	const fs::path&		inJackHmmer,
	uint32				inIterations,
	uint32				inMaxHits,
	float				inCutOff,
	ostream&			outHSSP)
{
	MChain* chain = new MChain('A');
	vector<MResidue*>& residues = chain->GetResidues();
	MResidue* last = nullptr;
	uint32 nr = 1;
	foreach (char r, inProtein)
	{
		residues.push_back(new MResidue(nr, r, last));
		++nr;
		last = residues.back();
	}
	
	vector<string> stockholmIds;
	stockholmIds.push_back(string("A=") + inProteinID);
	
	MProtein protein("UNDF", chain);
	CreateHSSP(inDatabank, protein, inDataDir, inFastaDir, inJackHmmer, inIterations, inMaxHits, stockholmIds, inCutOff, outHSSP);
}

void CreateHSSP(
	CDatabankPtr		inDatabank,
	const MProtein&		inProtein,
	const fs::path&		inDataDir,
	const fs::path&		inFastaDir,
	const fs::path&		inJackHmmer,
	uint32				inIterations,
	uint32				inMaxHits,
	vector<string>		inStockholmIds,
	float				inCutOff,
	ostream&			outHSSP)
{
	uint32 seqlength = 0;

	vector<mseq> alignments(inStockholmIds.size());
	vector<const MChain*> chains;
	vector<pair<uint32,uint32> > res_ranges;

	res_list res;
	hit_list hits;

	uint32 kchain = 0;
	foreach (string ch, inStockholmIds)
	{
		if (ch.length() < 3 or ch[1] != '=')
			THROW(("Invalid chain/stockholm pair specified: '%s'", ch.c_str()));

		const MChain& chain = inProtein.GetChain(ch[0]);
		chains.push_back(&chain);

		string seq;
		chain.GetSequence(seq);

		// strip off trailing X's. They are not very useful
		while (ba::ends_with(seq, "X"))
			seq.erase(seq.end() - 1);

		if (VERBOSE > 1)
			cerr << "Chain " << ch[0] << " => '" << seq << '\'' << endl;

		seqlength += seq.length();
		
		// alignments are stored in datadir
		fs::path afp;
		if (not inDataDir.empty())
			afp = inDataDir / (ch.substr(2) + ".aln.bz2");
		if (fs::exists(afp))
		{
			fs::path afp = inDataDir / (ch.substr(2) + ".aln.bz2");

			fs::ifstream af(afp, ios::binary);
			if (not af.is_open())
				THROW(("Could not open alignment file '%s'", afp.string().c_str()));
	
			if (VERBOSE)
				cerr << "Using fasta file '" << afp << '\'' << endl;
	
			io::filtering_stream<io::input> in;
			in.push(io::bzip2_decompressor());
			in.push(af);
	
			try {
				ReadFastA(in, alignments[kchain], seq, inMaxHits);
			}
			catch (...)
			{
				cerr << "exception while reading file " << afp << endl;
				throw;
			}
		}
		else if (not inJackHmmer.empty())
		{
			try
			{
				RunJackHmmer(seq, inIterations, inFastaDir, inJackHmmer, inDatabank->GetID(),
					afp, alignments[kchain]);
				
				if (not inDataDir.empty())
				{
					fs::ofstream ff(afp, ios::binary);
					if (not ff.is_open())
						THROW(("Could not create FastA file '%s'", afp.string().c_str()));
					
					io::filtering_stream<io::output> out;
					out.push(io::bzip2_compressor());
					out.push(ff);

					WriteFastA(out, alignments[kchain]);
				}
			}
			catch (...)
			{
				cerr << "exception while running jackhmmer for chain " << chain.GetChainID() << endl;
				throw;
			}
		}
		else
			THROW(("--no-jackhmmer specified and alignment is missing, exiting"));

		// Remove all hits that are not above the threshold here
		mseq& msa = alignments[kchain];
		msa.erase(remove_if(msa.begin() + 1, msa.end(), boost::bind(&seq::drop, _1, inCutOff)), msa.end());

		++kchain;
	}

	string usedChains;
	kchain = 0;
	foreach (const MChain* chain, chains)
	{
		if (not res.empty())
			res.push_back(res_ptr(new ResidueHInfo(res.size() + 1)));
		
		uint32 first = res.size();
		
		mseq& msa = alignments[kchain];
		ChainToHits(inDatabank, msa, *chain, hits, res);
		
		res_ranges.push_back(make_pair(first, res.size()));

		if (not usedChains.empty())
			usedChains += ',';
		usedChains += chain->GetChainID();

		++kchain;
	}

	sort(hits.begin(), hits.end(), compare_hit());

	if (inMaxHits > 0 and hits.size() > inMaxHits)
		hits.erase(hits.begin() + inMaxHits, hits.end());

	if (hits.empty())
		throw mas_exception("No hits found or remaining");
	
	uint32 nr = 1;
	foreach (hit_ptr h, hits)
		h->m_nr = nr++;

	for (uint32 c = 0; c < kchain; ++c)
	{
		pair<uint32,uint32> range = res_ranges[c];
		
		res_range r(res.begin() + range.first, res.begin() + range.second);
		CalculateConservation(alignments[c], r);

		foreach (res_ptr ri, r)
			ri->CalculateVariability(hits);
	}
	
	stringstream desc;
	if (inProtein.GetHeader().length() >= 50)
		desc << "HEADER     " + inProtein.GetHeader().substr(10, 40) << endl;
	if (inProtein.GetCompound().length() > 10)
		desc << "COMPND     " + inProtein.GetCompound().substr(10) << endl;
	if (inProtein.GetSource().length() > 10)
		desc << "SOURCE     " + inProtein.GetSource().substr(10) << endl;
	if (inProtein.GetAuthor().length() > 10)
		desc << "AUTHOR     " + inProtein.GetAuthor().substr(10) << endl;

	CreateHSSPOutput(inDatabank, inProtein.GetID(), desc.str(), inCutOff, seqlength,
		inProtein.GetChains().size(), kchain, usedChains, hits, res, outHSSP);
}

void CreateHSSP(
	CDatabankPtr					inDatabank,
	std::istream&					inAlignment,
	uint32							inMaxHits,
	float							inCutOff,
	std::ostream&					outHSSP)
{
	mseq msa;
	ReadFastA(inAlignment, msa, "", inMaxHits);
	msa.erase(remove_if(msa.begin() + 1, msa.end(), boost::bind(&seq::drop, _1, inCutOff)), msa.end());

	if (msa.size() < 2)
		throw mas_exception("no alignment");

	MChain* chain = new MChain('A');
	vector<MResidue*>& residues = chain->GetResidues();
	MResidue* last = nullptr;
	uint32 nr = 1;
	foreach (char r, msa.front())
	{
		if (is_gap(r))
			continue;

		residues.push_back(new MResidue(nr, r, last));
		++nr;
		last = residues.back();
	}

	MProtein protein("UNDF", chain);

	res_list res;
	hit_list hits;

	ChainToHits(inDatabank, msa, *chain, hits, res);

	sort(hits.begin(), hits.end(), compare_hit());

	if (inMaxHits > 0 and hits.size() > inMaxHits)
		hits.erase(hits.begin() + inMaxHits, hits.end());

	if (hits.empty())
		throw mas_exception("No hits found or remaining");
	
	nr = 1;
	foreach (hit_ptr h, hits)
		h->m_nr = nr++;

	res_range r(res.begin(), res.end());
	CalculateConservation(msa, r);

	foreach (res_ptr ri, r)
		ri->CalculateVariability(hits);
	
	stringstream desc;
	if (protein.GetHeader().length() >= 50)
		desc << "HEADER     " + protein.GetHeader().substr(10, 40) << endl;
	if (protein.GetCompound().length() > 10)
		desc << "COMPND     " + protein.GetCompound().substr(10) << endl;
	if (protein.GetSource().length() > 10)
		desc << "SOURCE     " + protein.GetSource().substr(10) << endl;
	if (protein.GetAuthor().length() > 10)
		desc << "AUTHOR     " + protein.GetAuthor().substr(10) << endl;

	CreateHSSPOutput(inDatabank, protein.GetID(), desc.str(), inCutOff, res.size(),
		protein.GetChains().size(), 1, "A", hits, res, outHSSP);
}

void ConvertHsspFile(
	const fs::path&	inHssp3File,
	const fs::path&	inHssp1File)
{
	fs::ifstream sf(inHssp3File, ios::binary);
	if (not sf.is_open())
		THROW(("Could not open input file '%s'", inHssp3File.string().c_str()));

	io::filtering_stream<io::input> in;
	if (inHssp3File.extension() == ".bz2")
		in.push(io::bzip2_decompressor());
	else if (inHssp3File.extension() == ".gz")
		in.push(io::gzip_decompressor());
	in.push(sf);

	for (;;)
	{
		ReadHSSP2File
		


		uint32 seqlength = 0;
	
		vector<mseq> alignments(inStockholmIds.size());
		vector<const MChain*> chains;
		vector<pair<uint32,uint32> > res_ranges;
	
		res_list res;
		hit_list hits;
	
		uint32 kchain = 0;
		foreach (string ch, inStockholmIds)
		{
			if (ch.length() < 3 or ch[1] != '=')
				THROW(("Invalid chain/stockholm pair specified: '%s'", ch.c_str()));
	
			const MChain& chain = inProtein.GetChain(ch[0]);
			chains.push_back(&chain);
	
			string seq;
			chain.GetSequence(seq);
	
			// strip off trailing X's. They are not very useful
			while (ba::ends_with(seq, "X"))
				seq.erase(seq.end() - 1);
	
			if (VERBOSE > 1)
				cerr << "Chain " << ch[0] << " => '" << seq << '\'' << endl;
	
			seqlength += seq.length();
			
			// alignments are stored in datadir
			fs::path afp;
			if (not inDataDir.empty())
				afp = inDataDir / (ch.substr(2) + ".aln.bz2");
			if (fs::exists(afp))
			{
				fs::path afp = inDataDir / (ch.substr(2) + ".aln.bz2");
	
				fs::ifstream af(afp, ios::binary);
				if (not af.is_open())
					THROW(("Could not open alignment file '%s'", afp.string().c_str()));
		
				if (VERBOSE)
					cerr << "Using fasta file '" << afp << '\'' << endl;
		
				io::filtering_stream<io::input> in;
				in.push(io::bzip2_decompressor());
				in.push(af);
		
				try {
					ReadFastA(in, alignments[kchain], seq, inMaxHits);
				}
				catch (...)
				{
					cerr << "exception while reading file " << afp << endl;
					throw;
				}
			}
			else if (not inJackHmmer.empty())
			{
				try
				{
					RunJackHmmer(seq, inIterations, inFastaDir, inJackHmmer, inDatabank->GetID(),
						afp, alignments[kchain]);
					
					if (not inDataDir.empty())
					{
						fs::ofstream ff(afp, ios::binary);
						if (not ff.is_open())
							THROW(("Could not create FastA file '%s'", afp.string().c_str()));
						
						io::filtering_stream<io::output> out;
						out.push(io::bzip2_compressor());
						out.push(ff);
	
						WriteFastA(out, alignments[kchain]);
					}
				}
				catch (...)
				{
					cerr << "exception while running jackhmmer for chain " << chain.GetChainID() << endl;
					throw;
				}
			}
			else
				THROW(("--no-jackhmmer specified and alignment is missing, exiting"));
	
			// Remove all hits that are not above the threshold here
			mseq& msa = alignments[kchain];
			msa.erase(remove_if(msa.begin() + 1, msa.end(), boost::bind(&seq::drop, _1, inCutOff)), msa.end());
	
			++kchain;
		}
	
		string usedChains;
		kchain = 0;
		foreach (const MChain* chain, chains)
		{
			if (not res.empty())
				res.push_back(res_ptr(new ResidueHInfo(res.size() + 1)));
			
			uint32 first = res.size();
			
			mseq& msa = alignments[kchain];
			ChainToHits(inDatabank, msa, *chain, hits, res);
			
			res_ranges.push_back(make_pair(first, res.size()));
	
			if (not usedChains.empty())
				usedChains += ',';
			usedChains += chain->GetChainID();
	
			++kchain;
		}
	
		sort(hits.begin(), hits.end(), compare_hit());
	
		if (inMaxHits > 0 and hits.size() > inMaxHits)
			hits.erase(hits.begin() + inMaxHits, hits.end());
	
		if (hits.empty())
			throw mas_exception("No hits found or remaining");
		
		uint32 nr = 1;
		foreach (hit_ptr h, hits)
			h->m_nr = nr++;
	
		for (uint32 c = 0; c < kchain; ++c)
		{
			pair<uint32,uint32> range = res_ranges[c];
			
			res_range r(res.begin() + range.first, res.begin() + range.second);
			CalculateConservation(alignments[c], r);
	
			foreach (res_ptr ri, r)
				ri->CalculateVariability(hits);
		}
		
		stringstream desc;
		if (inProtein.GetHeader().length() >= 50)
			desc << "HEADER     " + inProtein.GetHeader().substr(10, 40) << endl;
		if (inProtein.GetCompound().length() > 10)
			desc << "COMPND     " + inProtein.GetCompound().substr(10) << endl;
		if (inProtein.GetSource().length() > 10)
			desc << "SOURCE     " + inProtein.GetSource().substr(10) << endl;
		if (inProtein.GetAuthor().length() > 10)
			desc << "AUTHOR     " + inProtein.GetAuthor().substr(10) << endl;
	
	fs::ofstream ff(inHssp1File, ios::binary);
	if (not ff.is_open())
		THROW(("Could not create output file '%s'", inHssp1File.string().c_str()));
	
	io::filtering_stream<io::output> out;
	if (inHssp1File.extension() == ".bz2")
		out.push(io::bzip2_compressor());
	else if (inHssp1File.extension() == ".gz")
		out.push(io::gzip_compressor());
	out.push(ff);
	
	CreateHSSPOutput(inDatabank, inProtein.GetID(), desc.str(), inCutOff, seqlength,
		inProtein.GetChains().size(), kchain, usedChains, hits, res, out);
}


