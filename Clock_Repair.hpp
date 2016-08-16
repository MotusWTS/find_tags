#ifndef CLOCK_REPAIR_HPP
#define CLOCK_REPAIR_HPP

#include "find_tags_common.hpp"
#include "SG_Record.hpp"
#include "DB_Filer.hpp"
#include "Clock_Pinner.hpp"
#include "GPS_Validator.hpp"
#include <queue>

class Clock_Repair {

// ## Clock_Repair - a filter that repairs timestamps in SG records
//   
//   This class accepts a sequence of records from raw SG data files, and tries
//   to correct faulty timestamps.  Some or possibly even all records will be
//   buffered and then returned with corrected timestamps.
//   
//   This class would not be necessary if the SG on-board software and the GPS
//   were working correctly.
//
//   ## Timeline for timestamps:
//
//    Era:   |  MONOTONIC  |  PRE_GPS           |  VALID
//           +-------------+--------------------+------------->
//           |             |                    |
//    Value: 0             946684800            1262304000
//    Name:                TS_BEAGLEBONE_BOOT   TS_SG_EPOCH
//    Date:                2000-01-01           2010-01-01
//
//   ## Offset added to timestamp to correct, by Era:
//
//     Era       Offset
//   ============================
//   MONOTONIC   OFFSET_MONOTONIC
//   PRE_GPS     OFFSET_PRE_GPS
//   VALID       0
//  
//   i.e. timestamps in the VALID era don't need correcting.
//   
//   ## Sources of Timestamps
//   
//   - pulse records; these use the timestamp obtained from ALSA, which
//   in some releases uses CLOCK_MONOTONIC rather than CLOCK_REALTIME.
//   These timestamps need to be corrected by adding a fixed value, since
//   both these clocks are adjusted in the same way by chronyd, except
//   that CLOCK_REALTIME uses the unix epoch, while CLOCK_MONOTONIC uses
//   the reboot epoch.  CLOCK_REALTIME is incorrect (see below) until
//   stepped from a GPS fix, and this might not happen for minutes, hours,
//   or even days (with poor GPS sky view).
//   
//   - GPS records: the timestamp in these records is that obtained from
//   the GPS.  Normally, this timestamp is written to the raw data files
//   every 5 minutes, but if a user connects to the web interface and
//   manually asks to refresh the GPS fix, the result is also recorded to
//   raw files.  Sometimes, a GPS gets stuck (goes to sleep) and this
//   record does not vary.
//   
//   - parameter settings records: these are rare, and usually only at
//   the start of a boot session, when funcubes are first recognized and
//   set to their listening frequency.  The timestamp is from
//   CLOCK_REALTIME.
//   
//   ## Beaglebone Clock
//   
//   - the beaglebone CLOCK_REALTIME begins at 1 Jan 2000 00:00:00 GMT
//   when the unit boots.  This is 946684800 = TS_BEAGLEBONE_BOOT
//   Timestamps earlier than this are assumed to be from a monotonic
//   clock.
//   
//   - no SGs existed before 2010, so 1 Jan 2010 is a lower bound on valid
//   timestamps from raw SG files.  This is 1262304000 = TS_SG_EPOCH
//   
//   - we run chronyd to try sync the clock to GPS time, by stepping and
//   slewing.
//   
//   - correction to within a few seconds is done by a step from the GPS
//   at boot time, when possible.
//
// What needs doing:
//
// - for most receivers, look for the first jump in time from the non-VALID to
//   the VALID era, corresponding to the clock having been set from the GPS.
//   This jump is used as an estimate of OFFSET_PRE_GPS.  It should be corret
//   to within 5 minutes, the time between GPS records.
// 
// - for receivers where pulse timestamps are taken from CLOCK_MONOTONIC
//   instead of CLOCK_REALTIME, wait until timestamps on non-pulse records are
//   valid, then try to estimate the offset between CLOCK_REALTIME and
//   CLOCK_MONTONIC to within 1s.  This can be used to back-correct two kinds
//   of timestamps: 
//
//      - pulse timestamps on CLOCK_MONOTONIC: t <- t + OFFSET_MONTONIC
//
//      - param setting timestamps on CLOCK_REALTIME: t <- t - TS_BEAGLEBONE_BOOT + OFFSET_MONTONIC
//
// Design: two possibilities:
//
// - Filter: buffer SG records until we have a good clock, then pass them through to Tag_Foray
//     Pro:
//       - minimal change to Tag_Foray
//       - all clock correction logic stays in Clock_Repair class
//       - no need to code "restart" on sources
//     Con:
//       - it's possible that the entire batch will be buffered; e.g. if no GPS clock
//         setting happens at all.
//
// - Standalone: wait until we have a good clock, then restart the batch with corrections
//      Pro: 
//        - no buffering required
//      Con:
//        - won't work with input coming from a pipe
//        - splits up clock fixing logic between Clock_Repair and Tag_Foray
//        - need to implement restarts on data sources
//
// We go with the Filter design.
// This will require Tag_Foray to do something like:
//
//     done = false
//     while (! done) {
//       if (get_record_from_source()) {
//         put_record_to_Clock_Repair()
//       } else {
//         done = true;
//         tell_Clock_Repair_no_more_records()
//       }
//       while(Clock_Repair_get_record()) {
//         process_record()
//       }
//     }
//    
// ISSUES:
//
// - what if a boot session is split across multiple batches, and the first GPS fix isn't
//   until a later batch? Solution: serialize the Clock_Repair object; there would be no
//   output from such a batch until the subsequent fix.  Needs to be handled carefully.
//   Also, we then need to be able to indicate when there are no more records from a given
//   boot session, so that an error can be reported.  The R harness code will need to be involved.
//   The R harness could be used to provide OFFSET_GPS and OFFSET_MONOTONIC if the boot session
//   without a GPS clock fix was after a dated boot session.
//   
// - the error message field in the param setting record can be large; this will make buffered
//   records large.  Solution: leave out error message and pack struct.  Now 46 bytes.

public:

  Clock_Repair() {};

  Clock_Repair(DB_Filer * filer, Timestamp monoTol = 1); //!< ctor, with tolerance for bracketing correction to CLOCK_MONOTONIC

  //!< accept a record from an SG file
  void put( SG_Record & r);

  //!< indicate there are no more input records
  void done();

  //!< get the next record available for processing, and return true.
  // if no (corrected) records are available, return false.
  bool get(SG_Record &r);

protected:

  typedef enum {  // sources of timestamps (i.e. what kind of record in the raw file)
    TSS_PULSE = 0,  // pulse record
    TSS_GPS   = 1,  // GPS record
    TSS_PARAM = 2   // parameter setting record
  } TimestampSource;
  
  typedef enum {  // clock type from which timestamp was obtained
    CS_UNKNOWN          = -1, // not (yet?) determined
    CS_MONOTONIC        =  0, // monotonic SG system clock
    CS_REALTIME         =  1, // realtime SG system clock, after GPS sync
    CS_REALTIME_PRE_GPS =  2, // realtime SG system clock, before GPS sync
    CS_GPS              =  3  // GPS clock
  } ClockSource;

  static constexpr Timestamp TS_BEAGLEBONE_BOOT = 946684800;  // !<  2000-01-01 00:00:00
  static constexpr Timestamp TS_SG_EPOCH        = 1262304000; // !<  2010-01-01 00:00:00

  bool isValid(Timestamp ts) { return ts >= TS_SG_EPOCH; }; //!< is timestamp in VALID era?
  bool isMonotonic(Timestamp ts) { return ts < TS_BEAGLEBONE_BOOT; }; //!< is timestamp in MONOTONIC era?
  bool isPreGPS(Timestamp ts) { return ts >= TS_BEAGLEBONE_BOOT && ts < TS_SG_EPOCH; } //!< is timestamp in PRE_GPS era?

  DB_Filer * filer;   //!< for filing time corrections
  Timestamp monoTol;  //!< maximum allowed error (seconds) in correcting CLOCK_MONOTONIC timestamps
  Clock_Pinner cp;    //!< for pinning CLOCK_MONOTONIC to CLOCK_REALTIME
  GPS_Validator gpsv; //!< for detecting a stuck GPS

  std::queue < SG_Record > recBuf; //!< buffer records needing timestamp corrections, until we can do so

  bool correcting; //!< true if we're able to correct records
  bool havePreGPSoffset; //!< true if we've estimated the pre_gps_offset
  bool haveMonotonicOffset; //!< true if we've estimated the monotonic offset
  ClockSource pulseClock; //!< type of record provided by pulse clock
  // timestamps uing the MONOTONIC rather
  // REALTIME clock.  If true, we need to
  // pin the MONOTONIC clock by the tightest
  // possible bracket around a GPS timefix

  bool GPSstuck; // true iff we see the GPS is stuck; e.g. if two 
  // consecutive GPS timestamps are the same and at least 5 minutes of 
  // time has elapsed (as judged by timestamps from non-GPS record
  
  // Given a GPS fix is written every 5 minutes, we judge the GPS to
  // be stuck when we see this pattern:
  //
  // ---+-------+-----------+-----------+----->
  //    |       |           |           |
  //    GPSTS1  pulseTS1    pulseTS2    GPSTS2
  //
  //  with GPSTS2==GPSTS1 and pulseTS2 - pulseTS1 > 10 minutes
 
  Timestamp preGPSTS;
  Timestamp preGPSOffset;

  //!< We don't have a way to guarantee better accuracy of corrected pre-GPS timestamps 
  // than the interval of GPS timestamps, i.e. 5 minutes

  static constexpr Timestamp preGPSError = 5 * 60; 

  Timestamp monotonicTS;
  Timestamp monotonicOffset;
  Timestamp monotonicError;

  //!< has the clock been corrected?
  bool clock_okay();

  //!< are pulses using CLOCK_MONOTONIC?
  bool clock_monotonic();

  //!< are records left in the buffer?
  bool have_buffered();

  //!< do we have OFFSET_MONOTONIC?
  bool have_monotonic_offset();

  //!< return OFFSET_MONOTONIC
  Timestamp get_monotonic_offset();

  //!< return MONOTONIC timestamp at which offset was calculated
  Timestamp get_monotonic_ts();

  //!< do we have OFFSET_PRE_GPS
  bool have_pre_gps_offset();

  //!< return OFFSET_PRE_GPS
  Timestamp get_pre_gps_offset();

  //!< return PRE_GPS timestamp at which offset was calculated
  Timestamp get_pre_gps_ts();

public:

  // public serialize function.

  template<class Archive>
  void serialize(Archive & ar, const unsigned int version)
  {
    ar & BOOST_SERIALIZATION_NVP( monoTol );
    ar & BOOST_SERIALIZATION_NVP( cp );
    ar & BOOST_SERIALIZATION_NVP( gpsv );
    ar & BOOST_SERIALIZATION_NVP( recBuf );
    ar & BOOST_SERIALIZATION_NVP( correcting );
    ar & BOOST_SERIALIZATION_NVP( havePreGPSoffset );
    ar & BOOST_SERIALIZATION_NVP( haveMonotonicOffset );
    ar & BOOST_SERIALIZATION_NVP( pulseClock );
    ar & BOOST_SERIALIZATION_NVP( preGPSTS );
    ar & BOOST_SERIALIZATION_NVP( preGPSOffset );
    ar & BOOST_SERIALIZATION_NVP( monotonicTS );
    ar & BOOST_SERIALIZATION_NVP( monotonicOffset );
    ar & BOOST_SERIALIZATION_NVP( monotonicError );
  };  


};

  
#endif // CLOCK_REPAIR