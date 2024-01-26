-- This file provides the schema for the tags DB
-- It can be used to create an empty database to be populated by 
-- tag information

CREATE TABLE projs  (
   "id" INTEGER PRIMARY KEY NOT NULL,
   "name" TEXT,
   "label" TEXT,
   "tagsPermissions" INTEGER,
   "sensorsPermissions" INTEGER
);

CREATE TABLE tagDeps  (
   "tagID" INTEGER,
   "projectID" INTEGER,
   "deployID" INTEGER,
   "status" TEXT,
   "tsStart" REAL,
   "tsEnd" REAL,
   "deferSec" INTEGER,
   "speciesID" INTEGER,
   "markerNumber" TEXT,
   "markerType" TEXT,
   "latitude" REAL,
   "longitude" REAL,
   "elevation" REAL,
   "comments" TEXT,
   "id" INTEGER,
   "bi" REAL,
   "tsStartCode" INTEGER,
   "tsEndCode" INTEGER,
   "fullID" TEXT
);

CREATE INDEX tagDeps_tagID on tagDeps (tagID);
CREATE INDEX tagDeps_projectID on tagDeps (projectID);
CREATE INDEX tagDeps_deployID on tagDeps (deployID);

CREATE TABLE recvDeps  (
   "id" INTEGER,
   "serno" TEXT,
   "receiverType" TEXT,
   "deviceID" INTEGER,
   "macAddress" TEXT,
   "status" TEXT,
   "deployID" INTEGER,
   "name" TEXT,
   "fixtureType" TEXT,
   "latitude" REAL,
   "longitude" REAL,
   "isMobile" INTEGER,
   "tsStart" REAL,
   "tsEnd" REAL,
   "projectID" INTEGER,
   "elevation" REAL
);

CREATE INDEX recvDeps_deviceID on recvDeps (deviceID);
CREATE INDEX recvDeps_projectID on recvDeps (projectID);
CREATE INDEX recvDeps_deployID on recvDeps (deployID);

CREATE TABLE antDeps  (
   "deployID" INTEGER,
   "port" INTEGER,
   "antennaType" TEXT,
   "bearing" REAL,
   "heightMeters" REAL,
   "cableLengthMeters" REAL,
   "cableType" TEXT,
   "mountDistanceMeters" REAL,
   "mountBearing" REAL,
   "polarization2" REAL,
   "polarization1" REAL
);

CREATE INDEX antDeps_deployID on antDeps (deployID);

CREATE TABLE species (
   "id" INTEGER PRIMARY KEY NOT NULL,
   "english" TEXT,
   "french" TEXT,
   "scientific" TEXT,
   "group" TEXT,
   "sort" INTEGER
);

CREATE TABLE IF NOT EXISTS "recvGPS" (
   "deviceID" INTEGER,
   "ts" REAL,
   "lat" REAL,
   "lon" REAL,
   "elev" REAL,
   PRIMARY KEY ("deviceID", "ts")
);

CREATE TABLE IF NOT EXISTS 'meta' (key text primary key, val text);

CREATE TABLE serno_collision_rules (
    id INTEGER PRIMARY KEY NOT NULL,  -- unique ID for manipulation by API
    serno CHAR(16) NOT NULL,          -- receiver serial number (which
                                      -- is shared by 2 or more receivers)
    cond VARCHAR NOT NULL,            -- R expression involving
                                      -- components of the filename.  For SG receivers:
                                      --   prefix: short site code set on receiver
                                      --   bootnum: integer (uncorrected) boot count
                                      --   ts: timestamp of file as YYYY-MM-DDTHH-MM-SS.SSSS
                                      --   extension: usually `txt`
                                      --   comp: usually `gz`
                                      -- For Lotek receivers, the only component is `site_code`, a 4-digit string
                                      -- set by the user and retained by the receiver.
                                      -- When `cond` evaluates to TRUE for a file, the file is deemed to
                                      -- come from the receiver given by the serial number concatenated with `suffix`
    suffix VARCHAR NOT NULL           -- suffix this rule appends to serno if `cond` evaluates to TRUE
);

CREATE INDEX serno_collision_rules_serno on serno_collision_rules (serno);

CREATE TABLE paramOverrides (
-- This table records parameter overrides by
-- receiver serial number and boot session range (SG)
-- or timestamp range (Lotek).  Alternatively,
-- the override can apply to all receiver deployments for
-- the specified projectID, and the given time range or monoBN range.

    id INTEGER PRIMARY KEY NOT NULL,           -- unique ID for this override
    projectID INT,                             -- project ID for this override
    serno CHAR(32),                            -- serial number of device involved in this event (if any)
    tsStart FLOAT(53),                         -- starting timestamp for this override (Lotek only)
    tsEnd FLOAT(53),                           -- ending timestamp for this override (Lotek only)
    monoBNlow INT,                             -- starting boot session for this override (SG only)
    monoBNhigh INT,                            -- ending boot session for this override (SG only)
    progName VARCHAR(16) NOT NULL,             -- identifier of program; e.g. 'find_tags',
                                               -- 'lotek-plugins.so'
    paramName VARCHAR(16) NOT NULL,            -- name of parameter (e.g. 'minFreq')
    paramVal FLOAT(53),                        -- value of parameter (call be null if parameter is just a flag)
    why TEXT                                   -- human-readable reason for this override
);

CREATE INDEX paramOverrides_serno ON paramOverrides(serno);
CREATE INDEX paramOverrides_projectID ON paramOverrides(projectID);

CREATE TABLE IF NOT EXISTS "tags" (
  "tagID" INTEGER,
  "projectID" INTEGER,
  "mfgID" TEXT,
  "dateBin" TEXT,
  "type" TEXT,
  "codeSet" TEXT,
  "manufacturer" TEXT,
  "model" TEXT,
  "lifeSpan" INTEGER,
  "nomFreq" REAL,
  "offsetFreq" REAL,
  "period" REAL,
  "periodSD" REAL,
  "pulseLen" REAL,
  "param1" REAL,
  "param2" REAL,
  "param3" REAL,
  "param4" REAL,
  "param5" REAL,
  "param6" REAL,
  "param7" REAL,
  "param8" REAL,
  "tsSG" INTEGER,
  "approved" INTEGER,
  "deployID" INTEGER,
  "status" INTEGER,
  "tsStart" REAL,
  "tsEnd" REAL,
  "deferSec" REAL,
  "speciesID" INTEGER,
  "markerNumber" INTEGER,
  "markerType" TEXT,
  "latitude" REAL,
  "longitude" REAL,
  "elevation" REAL,
  "comments" TEXT,
  "id" INTEGER,
  "bi" REAL,
  "tsStartCode" INTEGER,
  "tsEndCode" INTEGER
);

CREATE TABLE IF NOT EXISTS "events" (
  "ts" REAL,
  "tagID" INTEGER,
  "event" INTEGER
);

CREATE INDEX events_ts on events(ts);

CREATE VIEW events_ as select strftime('%Y-%m-%d %H:%M:%f',ts,'unixepoch') as ts,tagID,event from events;
CREATE VIEW paramOverrides_ as select id,projectID,serno,strftime('%Y-%m-%d %H:%M:%f',tsStart,'unixepoch') as tsStart,strftime('%Y-%m-%d %H:%M:%f',tsEnd,'unixepoch') as tsEnd,monoBNlow,monoBNhigh,progName,paramName,paramVal,why from paramOverrides;
CREATE VIEW recvDeps_ as select id,serno,receiverType,deviceID,macAddress,status,deployID,name,fixtureType,latitude,longitude,isMobile,strftime('%Y-%m-%d %H:%M:%f',tsStart,'unixepoch') as tsStart,strftime('%Y-%m-%d %H:%M:%f',tsEnd,'unixepoch') as tsEnd,projectID,elevation from recvDeps;
CREATE VIEW recvGPS_ as select deviceID,strftime('%Y-%m-%d %H:%M:%f',ts,'unixepoch') as ts,lat,lon,elev from recvGPS;
CREATE VIEW tagDeps_ as select tagID,projectID,deployID,status,strftime('%Y-%m-%d %H:%M:%f',tsStart,'unixepoch') as tsStart,strftime('%Y-%m-%d %H:%M:%f',tsEnd,'unixepoch') as tsEnd,deferSec,speciesID,markerNumber,markerType,latitude,longitude,elevation,comments,id,bi,strftime('%Y-%m-%d %H:%M:%f',tsStartCode,'unixepoch') as tsStartCode,strftime('%Y-%m-%d %H:%M:%f',tsEndCode,'unixepoch') as tsEndCode,fullID from tagDeps;
CREATE VIEW tags_ as select tagID,projectID,mfgID,dateBin,type,codeSet,manufacturer,model,lifeSpan,nomFreq,offsetFreq,period,periodSD,pulseLen,param1,param2,param3,param4,param5,param6,param7,param8,strftime('%Y-%m-%d %H:%M:%f',tsSG,'unixepoch') as tsSG,approved,deployID,status,strftime('%Y-%m-%d %H:%M:%f',tsStart,'unixepoch') as tsStart,strftime('%Y-%m-%d %H:%M:%f',tsEnd,'unixepoch') as tsEnd,deferSec,speciesID,markerNumber,markerType,latitude,longitude,elevation,comments,id,bi,strftime('%Y-%m-%d %H:%M:%f',tsStartCode,'unixepoch') as tsStartCode,strftime('%Y-%m-%d %H:%M:%f',tsEndCode,'unixepoch') as tsEndCode from tags;


