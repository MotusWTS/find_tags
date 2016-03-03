#ifndef TAG_FORAY_HPP
#define TAG_FORAY_HPP

#include "find_tags_common.hpp"

#include "Tag_Database.hpp"

#include "Tag_Finder.hpp"
#include "Rate_Limiting_Tag_Finder.hpp"
#include <sqlite3.h>
#include <boost/serialization/utility.hpp>
#include <boost/serialization/list.hpp>
#include <boost/serialization/map.hpp>
#include <boost/serialization/set.hpp>
#include <boost/serialization/vector.hpp>
#include <boost/config.hpp>
#include <boost/mpl/eval_if.hpp>
#include <boost/mpl/identity.hpp>

using boost::serialization::make_nvp;

/*
  Tag_Foray - manager a collection of tag finders searching the same
  data stream.  The data stream has pulses from multiple ports, as
  well as frequency settings for those ports.
*/

class Tag_Foray {

public:

  Tag_Foray (); //!< default ctor to give object into which resume() deserializes
  
  Tag_Foray (Tag_Database * tags, std::istream * data, Frequency_MHz default_freq, bool force_default_freq, float min_dfreq, float max_dfreq,  float max_pulse_rate, Gap pulse_rate_window, Gap min_bogus_spacing, bool unsigned_dfreq=false);

  ~Tag_Foray ();

  long long start();                 // begin searching for tags; returns 0 if end of file; returns NN if receives command
                                     // !NEWBN,NN
  void process_event(Event e);       // !< process a tag add/remove event

  void test();                       // throws an exception if there are indistinguishable tags
  void graph();                      // graph the DFA for each nominal frequency

  void pause(); //!< serialize foray to output database

  static bool resume(Tag_Foray &tf); //!< resume foray from state saved in output database
  // returns true if successful

  static void set_default_pulse_slop_ms(float pulse_slop_ms);

  static void set_default_burst_slop_ms(float burst_slop_ms);

  static void set_default_burst_slop_expansion_ms(float burst_slop_expansion_ms);

  static void set_default_max_skipped_bursts(unsigned int skip);

  void set_data (std::istream * d);       // !< set the input data stream

  Tag_Database * tags;               // registered tags on all known nominal frequencies

  Timestamp now();                   // return time now as double timestamp

protected:
                                     // settings
  std::istream * data;               // stream from which data records are read
  Frequency_MHz default_freq;        // default listening frequency on a port where no frequency setting has been seen
  bool force_default_freq;           // ignore in-line frequency settings and always use default?
  float min_dfreq;                   // minimum allowed pulse offset frequency; pulses with smaller offset frequency are
                                     // discarded
  float max_dfreq;                   // maximum allowed pulse offset frequency; pulses with larger offset frequency are
                                     // discarded rate-limiting parameters:

  float max_pulse_rate;              // if non-zero, set a maximum per second pulse rate
  Gap pulse_rate_window;             // number of consecutive seconds over which rate of incoming pulses must exceed
                                     // max_pulse_rate in order to discard entire window
  Gap min_bogus_spacing;             // when a window of pulses is discarded, we emit a bogus tag with ID 0.; this parameter
                                     // sets the minimum number of seconds between consecutive emissions of this bogus tag ID

  bool unsigned_dfreq;               // if true, ignore any sign on frequency offsets (use absolute value)

  // runtime storage

  unsigned long long line_no;                    // count lines of input seen
  
  typedef short Port_Num;                        // port number can be represented as a short

  std::map < Port_Num, Freq_Setting > port_freq; // keep track of frequency settings on each port


  // we need a Tag_Finder for each combination of port and nominal frequency
  // we'll use a map

  typedef std::pair < Port_Num, Nominal_Frequency_kHz > Tag_Finder_Key;
  typedef std::map < Tag_Finder_Key, Tag_Finder * > Tag_Finder_Map;

  Tag_Finder_Map tag_finders;

  std::map < Nominal_Frequency_kHz, Graph * > graphs;

  Gap pulse_slop;	// (seconds) allowed slop in timing between
			// burst pulses,
  // in seconds for each pair of
  // consecutive pulses in a burst, this
  // is the maximum amount by which the
  // gap between the pair can differ
  // from the gap between the
  // corresponding pair in a registered
  // tag, and still match the tag.

  Gap burst_slop;	// (seconds) allowed slop in timing between
                        // consecutive tag bursts, in seconds this is
                        // meant to allow for measurement error at tag
                        // registration and detection times


  Gap burst_slop_expansion; // (seconds) how much slop in timing
			    // between tag bursts increases with each
  // skipped pulse; this is meant to allow for clock drift between
  // the tag and the receiver.

  // how many consecutive bursts can be missing without terminating a
  // run?

  unsigned int max_skipped_bursts;

  History *hist;
  Ticker cron;

  double ts; // last timestamp parsed from input file

  static Gap default_pulse_slop;
  static Gap default_burst_slop;
  static Gap default_burst_slop_expansion;
  static unsigned int default_max_skipped_bursts;

public:

  // public serialize function.

  template<class Archive>
  void serialize(Archive & ar, const unsigned int version)
  {
    ar & BOOST_SERIALIZATION_NVP( tags );
    ar & BOOST_SERIALIZATION_NVP( default_freq );
    ar & BOOST_SERIALIZATION_NVP( force_default_freq );
    ar & BOOST_SERIALIZATION_NVP( min_dfreq );
    ar & BOOST_SERIALIZATION_NVP( max_dfreq );
    ar & BOOST_SERIALIZATION_NVP( max_pulse_rate );
    ar & BOOST_SERIALIZATION_NVP( pulse_rate_window );
    ar & BOOST_SERIALIZATION_NVP( min_bogus_spacing );
    ar & BOOST_SERIALIZATION_NVP( unsigned_dfreq );
    ar & BOOST_SERIALIZATION_NVP( line_no );
    ar & BOOST_SERIALIZATION_NVP( port_freq );
    ar & BOOST_SERIALIZATION_NVP( tag_finders );
    ar & BOOST_SERIALIZATION_NVP( graphs );
    ar & BOOST_SERIALIZATION_NVP( pulse_slop );
    ar & BOOST_SERIALIZATION_NVP( burst_slop );
    ar & BOOST_SERIALIZATION_NVP( burst_slop_expansion );
    ar & BOOST_SERIALIZATION_NVP( max_skipped_bursts );
    ar & BOOST_SERIALIZATION_NVP( hist );
    ar & BOOST_SERIALIZATION_NVP( cron );
  };  
};


// serialize std::unordered_set

template< class Member >
void serialize(
               boost::archive::binary_oarchive &ar,
               std::unordered_set < Member > &s,
               const unsigned int file_version
               ){
  save(ar, s, file_version);
};

template< class Member >
void serialize(
               boost::archive::binary_iarchive &ar,
               std::unordered_set < Member > &s,
               const unsigned int file_version
               ){
  load(ar, s, file_version);
};

template< class Member >
void save(boost::archive::binary_oarchive & ar, std::unordered_set < Member > & s, const unsigned int version) {
  
  size_t n = s.size();
  ar & BOOST_SERIALIZATION_NVP( n );
  for (auto i = s.begin(); i != s.end(); ++i)
    ar & make_nvp("m", *i);
};

template<class Archive, class Member>
void load(Archive & ar, std::unordered_set < Member > & s, const unsigned int version) {

  size_t n;
  ar & BOOST_SERIALIZATION_NVP( n );
  for (size_t i = 0; i < n; ++i) {
    Member m;
    ar & BOOST_SERIALIZATION_NVP( m );
    s.insert(m);
  }
};

// serialize std::unordered_multiset

template< class Member >
void serialize(
               boost::archive::binary_oarchive &ar,
               std::unordered_multiset < Member > &s,
               const unsigned int file_version
               ){
  save(ar, s, file_version);
};

template< class Member >
void serialize(
               boost::archive::binary_iarchive &ar,
               std::unordered_multiset < Member > &s,
               const unsigned int file_version
               ){
  load(ar, s, file_version);
};


template< class Member >
void save(boost::archive::binary_oarchive & ar, std::unordered_multiset < Member > & s, const unsigned int version) {
  
  size_t n = s.size();
  ar & BOOST_SERIALIZATION_NVP( n );
  for (auto i = s.begin(); i != s.end(); ++i)
    ar & make_nvp("m", *i);
};

template<class Archive, class Member>
void load(Archive & ar, std::unordered_multiset < Member > & s, const unsigned int version) {

  size_t n;
  ar & BOOST_SERIALIZATION_NVP( n );
  for (size_t i = 0; i < n; ++i) {
    Member m;
    ar & BOOST_SERIALIZATION_NVP( m );
    s.insert(m);
  }
};

// serialize std::unordered_multimap

template< class Key, class Value >
void serialize(
               boost::archive::binary_oarchive &ar,
               std::unordered_multimap < Key, Value > &s,
               const unsigned int file_version
               ){
  save(ar, s, file_version);
};

template< class Key, class Value >
void serialize(
               boost::archive::binary_iarchive &ar,
               std::unordered_multimap < Key, Value > &s,
               const unsigned int file_version
               ){
  load(ar, s, file_version);
};


template< class Key, class Value >
void save(boost::archive::binary_oarchive & ar, std::unordered_multimap < Key, Value > & s, const unsigned int version) {
  
  size_t n = s.size();
  ar & BOOST_SERIALIZATION_NVP( n );
  for (auto i = s.begin(); i != s.end(); ++i) {
    ar & make_nvp("k", i->first);
    ar & make_nvp("v", i->second);
  };

};

template<class Archive, class Key, class Value>
void load(Archive & ar, std::unordered_multimap < Key, Value > & s, const unsigned int version) {

  size_t n;
  ar & BOOST_SERIALIZATION_NVP( n );
  for (size_t i = 0; i < n; ++i) {
    Key k;
    Value v;
    ar & BOOST_SERIALIZATION_NVP( k );
    ar & BOOST_SERIALIZATION_NVP( v );
    s.insert(std::make_pair(k, v));
  }
};

#endif // TAG_FORAY
