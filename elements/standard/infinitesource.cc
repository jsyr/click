/*
 * infinitesource.{cc,hh} -- element generates configurable infinite stream
 * of packets
 * Eddie Kohler
 *
 * Copyright (c) 1999-2000 Massachusetts Institute of Technology
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, subject to the conditions
 * listed in the Click LICENSE file. These conditions include: you must
 * preserve this copyright notice, and you cannot mention the copyright
 * holders in advertising related to the Software without their permission.
 * The Software is provided WITHOUT ANY WARRANTY, EXPRESS OR IMPLIED. This
 * notice is a summary of the Click LICENSE file; the license in that file is
 * legally binding.
 */

#include <click/config.h>
#include "infinitesource.hh"
#include <click/confparse.hh>
#include <click/error.hh>
#include <click/router.hh>
#include <click/standard/scheduleinfo.hh>
#include <click/glue.hh>
CLICK_DECLS

InfiniteSource::InfiniteSource()
  : Element(0, 1), _packet(0), _task(this)
{
}

InfiniteSource::~InfiniteSource()
{
}

void *
InfiniteSource::cast(const char *n) 
{
  if (strcmp(n, "InfiniteSource") == 0) 
    return (InfiniteSource *)this;
  else if (strcmp(n, Notifier::EMPTY_NOTIFIER) == 0) 
    return static_cast<Notifier *>(this);
  else
    return 0;
}

int
InfiniteSource::configure(Vector<String> &conf, ErrorHandler *errh)
{
  ActiveNotifier::initialize(router());
  String data = "Random bullshit in a packet, at least 64 bytes long. Well, now it is.";
  int limit = -1;
  int burstsize = 1;
  int datasize = -1;
  bool active = true, stop = false;

  if (cp_va_parse(conf, this, errh,
		  cpOptional,
		  cpString, "packet data", &data,
		  cpInteger, "total packet count", &limit,
		  cpInteger, "burst size (packets per scheduling)", &burstsize,
		  cpBool, "active?", &active,
		  cpKeywords,
		  "DATA", cpString, "packet data", &data,
		  "DATASIZE", cpInteger, "minimum packet size", &datasize,
		  "LIMIT", cpInteger, "total packet count", &limit,
		  "BURST", cpInteger, "burst size (packets per scheduling)", &burstsize,
		  "ACTIVE", cpBool, "active?", &active,
		  "STOP", cpBool, "stop driver when done?", &stop,
		  cpEnd) < 0)
    return -1;
  if (burstsize < 1)
    return errh->error("burst size must be >= 1");

  _data = data;
  _datasize = datasize;
  _limit = limit;
  _burstsize = burstsize;
  _count = 0;
  _active = active;
  _stop = stop;

  setup_packet();
  
  return 0;
}

int
InfiniteSource::initialize(ErrorHandler *errh)
{
  if (output_is_push(0)) {
    ScheduleInfo::initialize_task(this, &_task, errh);
    _nonfull_signal = Notifier::downstream_nonfull_signal(this, 0, &_task);
  }
  return 0;
}

void
InfiniteSource::cleanup(CleanupStage)
{
  if (_packet)
    _packet->kill();
}

bool
InfiniteSource::run_task()
{
    if (!_active || !_nonfull_signal)
	return false;
    int n = _burstsize;
    if (_limit >= 0 && _count + n >= _limit)
	n = _limit - _count;
    if (n <= 0)
	return false;
    for (int i = 0; i < n; i++) {
	Packet *p = _packet->clone();
	p->timestamp_anno().set_now();
	output(0).push(p);
    }
    _count += n;
    if (_stop && _limit >= 0 && _count >= _limit)
	router()->please_stop_driver();
    _task.fast_reschedule();
    return true;
}

Packet *
InfiniteSource::pull(int)
{
  if (!_active) {
    if (signal_active()) {
      sleep_listeners();
    }
    return 0;
  }
  if (_limit >= 0 && _count >= _limit) {
    if (_stop)
      router()->please_stop_driver();

    if (signal_active()) {
      sleep_listeners();
    }
    return 0;
  }
  _count++;
  Packet *p = _packet->clone();
  p->timestamp_anno().set_now();
  return p;
}

void
InfiniteSource::setup_packet() 
{
  if (_packet) _packet->kill();

  if (_datasize != -1 && _datasize > _data.length()) {
    // make up some data to fill extra space
    String new_data;
    do {
      new_data += _data;
    }
    while (new_data.length() < _datasize);    
    _packet = Packet::make(new_data.data(), _datasize);
  }
  else
    _packet = Packet::make(_data.data(), _data.length());
}

String
InfiniteSource::read_param(Element *e, void *vparam)
{
  InfiniteSource *is = (InfiniteSource *)e;
  switch ((intptr_t)vparam) {
   case 0:			// data
    return is->_data;
   case 1:			// limit
    return String(is->_limit) + "\n";
   case 2:			// burstsize
    return String(is->_burstsize) + "\n";
   case 3:			// active
    return cp_unparse_bool(is->_active) + "\n";
   case 4:			// count
    return String(is->_count) + "\n";
   case 6:			// datasize
    return String(is->_datasize) + "\n";
   default:
    return "";
  }
}

int
InfiniteSource::change_param(const String &in_s, Element *e, void *vparam,
			     ErrorHandler *errh)
{
  InfiniteSource *is = (InfiniteSource *)e;
  String s = cp_uncomment(in_s);
  switch ((intptr_t)vparam) {

   case 0: {			// data
     String data;
     if (!cp_string(s, &data))
       return errh->error("data parameter must be string");
     is->_data = data;
     is->setup_packet();
     break;
   }
   
   case 1: {			// limit
     int limit;
     if (!cp_integer(s, &limit))
       return errh->error("limit parameter must be integer");
     is->_limit = limit;
     break;
   }
   
   case 2: {			// burstsize
     int burstsize;
     if (!cp_integer(s, &burstsize) || burstsize < 1)
       return errh->error("burstsize parameter must be integer >= 1");
     is->_burstsize = burstsize;
     break;
   }
   
   case 3: {			// active
     bool active;
     if (!cp_bool(s, &active))
       return errh->error("active parameter must be boolean");
     is->_active = active;
     break;
   }

   case 5: {			// reset
     is->_count = 0;
     break;
   }

   case 6: {			// datasize
     int datasize;
     if (!cp_integer(s, &datasize) || datasize < 1)
       return errh->error("datasize parameter must be integer >= 1");
     is->_datasize = datasize;
     is->setup_packet();
     break;
   }
  }

  if (is->_active && (is->_limit < 0 || is->_count < is->_limit)) {
    if (is->output_is_push(0) && !is->_task.scheduled())
      is->_task.reschedule();
    
    if (is->output_is_pull(0) && !is->signal_active())
      is->wake_listeners();
  }
  return 0;
}

void
InfiniteSource::add_handlers()
{
  add_read_handler("data", read_param, (void *)0);
  add_write_handler("data", change_param, (void *)0);
  add_read_handler("limit", read_param, (void *)1);
  add_write_handler("limit", change_param, (void *)1);
  add_read_handler("burstsize", read_param, (void *)2);
  add_write_handler("burstsize", change_param, (void *)2);
  add_read_handler("active", read_param, (void *)3);
  add_write_handler("active", change_param, (void *)3);
  add_read_handler("count", read_param, (void *)4);
  add_write_handler("reset", change_param, (void *)5);
  add_read_handler("datasize", read_param, (void *)6);
  add_write_handler("datasize", change_param, (void *)6);
  add_task_handlers(&_task);
}

CLICK_ENDDECLS
EXPORT_ELEMENT(InfiniteSource)
