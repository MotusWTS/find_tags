#include "DB_Filer.hpp"
#include <stdio.h>

DB_Filer::DB_Filer (const string &out, const string &prog_name, const string &prog_version, double prog_ts): 
  prog_name(prog_name),
  num_runs(0),
  num_hits(0),
  num_steps(0)
{
  Check(sqlite3_open_v2(out.c_str(),
                        & outdb,
                        SQLITE_OPEN_READWRITE,
                        0),
    "Output database file does not exist.");

  Check( sqlite3_exec(outdb,
                      "insert into batches (monoBN) values (-1)",
                      0,
                      0,
                      0),
         "SQLite output database does not have valid 'batches' table.");

  // get automatically-generated batch ID; used to update batch record at end of run
  bid = sqlite3_last_insert_rowid(outdb);

  sprintf(qbuf, "insert into runs (runID, batchID, motusTagID, tsMotus) \
                               values (?,     %lld,   ?,          0)",
           bid);

  string msg = "output DB table 'runs' is invalid";
  Check( sqlite3_prepare_v2(outdb, qbuf, -1, &st_begin_run, 0), msg);
  Check( sqlite3_prepare_v2(outdb, "update runs set len=? where runID=?", -1, &st_end_run, 0),
         msg);

  Check(sqlite3_prepare_v2(outdb, 
                           "insert into hits (runID, ant, ts, sig, sigSD, noise, freq, freqSD, slop, burstSlop) \
                                    values (?,     ?,   ?,  ?,   ?,     ?,     ?,    ?,      ?,    ?)",
                           -1, &st_add_hit, 0),
        "output DB does not have valid 'hits' table.");

  sprintf(qbuf, 
          "insert into batchProgs (batchID, progName, progVersion, progBuildTS, tsMotus) \
                           values (%lld,    '%s',     '%s',        %f,          0)",
          bid,
          prog_name.c_str(),
          prog_version.c_str(),
          prog_ts);

  Check( sqlite3_exec(outdb,
                      qbuf,
                      0,
                      0,
                      0),
    "output DB does not have valid 'batchProgs' table.");

  Check( sqlite3_prepare_v2(outdb, 
                            "insert into batchParams (batchID, progName, paramName, paramVal, tsMotus) \
                                           values (?,       ?,        ?,         ?,          0)",
                            -1, &st_add_param, 0),
         "output DB does not have valid 'batchParams' table.");

  sqlite3_stmt * st_get_rid;
  Check( sqlite3_prepare_v2(outdb,
                            "select max(runID) from runs",
                            -1,
                            & st_get_rid,
                            0),
         "SQLite output database does not have valid 'runs' table - missing runID?");

  sqlite3_step(st_get_rid);
  rid = 1 + sqlite3_column_int64(st_get_rid, 0);
  sqlite3_finalize(st_get_rid);
  st_get_rid = 0;

  // begin first transaction
  sqlite3_exec(outdb, "begin", 0, 0, 0);
};

DB_Filer::~DB_Filer() {
  sprintf(qbuf, 
          "update batches set numRuns=%d, numHits=%lld where ID=%lld",
          num_runs, num_hits, bid);

  Check( sqlite3_exec(outdb,
                      qbuf,
                      0,
                      0,
                      0),
         "Failed to update batches record in output database.");

  sqlite3_finalize(st_begin_run);
  sqlite3_finalize(st_end_run);
  sqlite3_finalize(st_add_hit);
  Check( sqlite3_exec(outdb, "commit", 0, 0, 0), "Failed to commit remaining inserts.");
  sqlite3_close(outdb);
  outdb = 0;
};

DB_Filer::Run_ID
DB_Filer::begin_run(Motus_Tag_ID mid) {
  sqlite3_bind_int64(st_begin_run, 1, rid); // bind run ID
  sqlite3_bind_int64(st_begin_run, 2, mid); // bind tag ID
  step_commit(st_begin_run);
  return rid++;
};

void
DB_Filer::end_run(Run_ID rid, int n) {
  sqlite3_bind_int(st_end_run, 1, n); // bind number of hits in run
  sqlite3_bind_int64(st_end_run, 2, rid);  // bind run number
  step_commit(st_end_run);
  ++ num_runs;
};


void
DB_Filer::add_hit(Run_ID rid, char ant, double ts, float sig, float sigSD, float noise, float freq, float freqSD, float slop, float burstSlop) {
  sqlite3_bind_int64 (st_add_hit, 1, rid);
  sqlite3_bind_int   (st_add_hit, 2, ant-'0');
  sqlite3_bind_double(st_add_hit, 3, ts);
  sqlite3_bind_double(st_add_hit, 4, sig);
  sqlite3_bind_double(st_add_hit, 5, sigSD);
  sqlite3_bind_double(st_add_hit, 6, noise);
  sqlite3_bind_double(st_add_hit, 7, freq);
  sqlite3_bind_double(st_add_hit, 8, freqSD);
  sqlite3_bind_double(st_add_hit, 9, slop);
  sqlite3_bind_double(st_add_hit, 10, burstSlop);
  step_commit(st_add_hit);
  ++ num_hits;
};

void
DB_Filer::step_commit(sqlite3_stmt * st) {
  Check(sqlite3_step(st), "unable to step statement", SQLITE_DONE);
  sqlite3_reset(st);
  if (++num_steps == steps_per_tx) {
    sqlite3_exec(outdb, "commit", 0, 0, 0);
    sqlite3_exec(outdb, "begin" , 0, 0, 0);
    num_steps = 0;
  }
}

char
DB_Filer::qbuf[256];

void
DB_Filer::add_param(const string &name, double value) {
  sqlite3_bind_int64(st_add_param, 1, bid);
  // we use SQLITE_TRANSIENT in the following to make a copy, otherwise
  // the caller's copy might be destroyed before this transaction is committed
  sqlite3_bind_text(st_add_param, 2, prog_name.c_str(), -1, SQLITE_TRANSIENT); 
  sqlite3_bind_text(st_add_param, 3, name.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_double(st_add_param, 4, value);
  step_commit(st_add_param);
};

void
DB_Filer::Check(int code, const std::string & err, int wants) {
  if (code != wants)
    throw std::runtime_error(err + "\nSqlite error: " + sqlite3_errmsg(outdb));
};
