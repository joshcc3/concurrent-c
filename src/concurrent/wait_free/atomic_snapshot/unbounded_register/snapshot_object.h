#ifndef SNAP_H
#define SNAP_H

typedef int procid_t;
typedef int value;
typedef int seq_t;

typedef struct snapshot {
  const value* values;
  const seq_t* seqs;
} snapshot;

typedef struct proc_local {
  value val;
  seq_t seq;
  const snapshot* snap_base;
} proc_local;

typedef struct atomic_object {
  proc_local * shared;
  int num_procs;
} atomic_object;


void init_ao(int, atomic_object*);

void ao_update(atomic_object, procid_t, value);
void ao_snap(atomic_object, procid_t, snapshot*);

void print_snap(snapshot*);

#endif
