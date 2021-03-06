#ifndef TAG_FORAY_HPP
#define TAG_FORAY_HPP

#include "find_tags_common.hpp"

#include "Tag_Database.hpp"

#include "Tag_Finder.hpp"
#include "Rate_Limiting_Tag_Finder.hpp"
#include "Data_Source.hpp"
#include "DB_Filer.hpp"
#include "Clock_Repair.hpp"

#include <sqlite3.h>
#include <boost/serialization/deque.hpp>
#include <boost/serialization/list.hpp>
#include <boost/serialization/map.hpp>
#include <boost/serialization/queue.hpp>
#include <boost/serialization/set.hpp>
#include <boost/serialization/unordered_map.hpp>
#include <boost/serialization/unordered_set.hpp>
#include <boost/serialization/utility.hpp>
#include <boost/serialization/vector.hpp>
#include <boost/config.hpp>
#include <boost/mpl/eval_if.hpp>
#include <boost/mpl/identity.hpp>

using boost::serialization::make_nvp;

/*
  Tag_Foray - manage a collection of tag finders searching the same
  data stream.  The data stream has pulses from multiple ports, as
  well as frequency settings for those ports.
*/

class Tag_Foray {

public:

  Tag_Foray (); //!< default ctor to give object into which resume() deserializes
  ~Tag_Foray (); //!< dtor which deletes Tag_Finders and their confirmed candidates, so runs are correctly ended
  Tag_Foray (Tag_Database * tags, Data_Source * data, Frequency_MHz default_freq, bool force_default_freq, float min_dfreq, float max_dfreq,  float max_pulse_rate, Gap pulse_rate_window, Gap min_bogus_spacing, bool unsigned_dfreq=false, bool pulses_only=false);

  void start();                 // begin searching for tags

  void process_event(Event e);       // !< process a tag add/remove event

  void test();                       // throws an exception if there are indistinguishable tags
  void graph();                      // graph the DFA for each nominal frequency

  void pause(); //!< serialize foray to output database

  static bool resume(Tag_Foray &tf, Data_Source *data, long long bootnum); //!< resume foray from state saved in output database
  // returns true if successful

  static void set_default_pulse_slop_ms(float pulse_slop_ms);

  static void set_default_burst_slop_ms(float burst_slop_ms);

  static void set_default_burst_slop_expansion_ms(float burst_slop_expansion_ms);

  static void set_default_max_skipped_bursts(unsigned int skip);

  static void set_timestamp_wonkiness(unsigned int w);

  static int num_cands_with_run_id(DB_Filer::Run_ID rid, int delta); //!< return the number of candidates with the given run id
  // if delta is 0. Otherwise, adjust the count by delta, and return the new count.


  Tag_Database * tags;               // registered tags on all known nominal frequencies

  Timestamp last_seen() {return ts;}; // return last timestamp seen on input

#ifdef ACTIVE_TAG_DIAGNOSTICS
  // dump tags active for each Tag_Finder to cout
  void dump_active_tags(double ts);
  // set active tag dump interval
  // i.e. how often are lists of active tags dumped? (seconds in data time)?
  // only happens if t > 0
  static void set_active_tag_dump_interval(double t);
#endif // ACTIVE_TAG_DIAGNOSTICS

  static constexpr double MIN_VALID_TIMESTAMP = 1262304000; // unix timestamp for 1 Jan 2010, GMT
  static constexpr double BEAGLEBONE_POWERUP_TS = 946684800; // unix timestamp for 1 Jan 2000, GMT

  // Serialization version.  Whenever the format of serialized data
  // changes (i.e. changes are made to any class's `serialize` method,
  // or to how `Tag_Foray::pause()` writes data, this version must be
  // changed appropriately so that find_tags_motus doesn't resume from
  // incompatible state, which might cause a segfault, or worse,
  // incorrect data!

  // We'll distinguish between minor and major serialization versions:
  //
  //  - serialized state is incompatible across changes to major version
  //  - serialized state is compatible across changes to minor version, but class methods
  //  have to deal with it.
  //  The serialization version will be (major << 16) | minor

  // VERSION 2.0: gzip-compressed

  static constexpr int SERIALIZATION_MAJOR_VERSION = 2;
  static constexpr int SERIALIZATION_MINOR_VERSION = 0;
  static constexpr int SERIALIZATION_VERSION = (SERIALIZATION_MAJOR_VERSION << 16) | SERIALIZATION_MINOR_VERSION;

protected:

  Data_Source * data;                // stream from which data records are read
  Clock_Repair * cr;                 // filter to fix timestamps in input
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

  bool pulses_only;                  // if true, only output pulses, don't run Tag_Finders

  // runtime storage

  unsigned long long line_no;                    // count lines of input seen

  std::map < Port_Num, Freq_Setting > port_freq; // keep track of frequency settings on each port

  std::vector < int > pulse_count;     // keep track of hourly counts of pulses on each port

  // we need a Tag_Finder for each combination of port and nominal frequency
  // we'll use a map

  typedef std::pair < Port_Num, Nominal_Frequency_kHz > Tag_Finder_Key;
  typedef std::map < Tag_Finder_Key, Tag_Finder * > Tag_Finder_Map;

  Tag_Finder_Map tag_finders;

  double ts; // for retaining last timestamp

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

  double tsBegin; // first timestamp parsed from input file
  double prevHourBin; // previous hourly bin, for counting pulses

  static Gap default_pulse_slop;
  static Gap default_burst_slop;
  static Gap default_burst_slop_expansion;
  static unsigned int default_max_skipped_bursts;
  static unsigned int timestamp_wonkiness; //!< maximum clock jump size in data from Lotek .DTA files

  // keep track of how many candidates share the same run; this is
  // to manage clones at the confirmed level, so that death of a single
  // clone does not end a run.

  typedef std::unordered_map < DB_Filer::Run_ID, int > Run_Cand_Counter;
  static  Run_Cand_Counter num_cands_with_run_id_;

#ifdef ACTIVE_TAG_DIAGNOSTICS
  // interval at which active tag list is dumped for each Tag_Finder
  // only used if > 0
  static double active_tag_dump_interval;

  // timestamp at which next dump of active tags is required
  // (only valid if active_tag_dump_interval > 0)
  static double next_active_tag_dump_time;
#endif // ACTIVE_TAG_DIAGNOSTICS

public:

  // public serialize function.

  template<class Archive>
  void serialize(Archive & ar, const unsigned int version)
  {
    ar & BOOST_SERIALIZATION_NVP( tags );
    ar & BOOST_SERIALIZATION_NVP( cr );
    ar & BOOST_SERIALIZATION_NVP( default_freq );
    ar & BOOST_SERIALIZATION_NVP( force_default_freq );
    ar & BOOST_SERIALIZATION_NVP( min_dfreq );
    ar & BOOST_SERIALIZATION_NVP( max_dfreq );
    ar & BOOST_SERIALIZATION_NVP( max_pulse_rate );
    ar & BOOST_SERIALIZATION_NVP( pulse_rate_window );
    ar & BOOST_SERIALIZATION_NVP( min_bogus_spacing );
    ar & BOOST_SERIALIZATION_NVP( unsigned_dfreq );
    ar & BOOST_SERIALIZATION_NVP( pulses_only );
    ar & BOOST_SERIALIZATION_NVP( line_no );
    ar & BOOST_SERIALIZATION_NVP( port_freq );
    ar & BOOST_SERIALIZATION_NVP( pulse_count );
    ar & BOOST_SERIALIZATION_NVP( tag_finders );
    ar & BOOST_SERIALIZATION_NVP( ts );
    ar & BOOST_SERIALIZATION_NVP( graphs );
    ar & BOOST_SERIALIZATION_NVP( pulse_slop );
    ar & BOOST_SERIALIZATION_NVP( burst_slop );
    ar & BOOST_SERIALIZATION_NVP( burst_slop_expansion );
    ar & BOOST_SERIALIZATION_NVP( max_skipped_bursts );
    ar & BOOST_SERIALIZATION_NVP( hist );
    ar & BOOST_SERIALIZATION_NVP( cron );
  };
};

#endif // TAG_FORAY
