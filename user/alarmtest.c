//
// test program for the alarm lab.
// you can modify this file for testing,
// but please make sure your kernel
// modifications pass the original
// versions of these tests.
//

#include "kernel/param.h"
#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/riscv.h"
#include "user/user.h"

void test0();
void test1();
void test2();
void test3();
void test4();
void test5();
void test6();
void test7();
void periodic();
void slow_handler();
void syscall_handler();
void parent_handler();
void child_handler();
void interval_handler();

int
main(int argc, char *argv[])
{
  test0();
  test1();
  test2();
  test3();
  test4();
  test5();
  test6();
  test7();
  exit(0);
}

volatile static int count;

void
periodic()
{
  count = count + 1;
  printf("alarm!\n");
  sigreturn();
}

// tests whether the kernel calls
// the alarm handler even a single time.
void
test0()
{
  int i;
  printf("test0 start\n");
  count = 0;
  sigalarm(2, periodic);
  for(i = 0; i < 1000*500000; i++){
    if((i % 1000000) == 0)
      write(2, ".", 1);
    if(count > 0)
      break;
  }
  sigalarm(0, 0);
  if(count > 0){
    printf("test0 passed\n");
  } else {
    printf("\ntest0 failed: the kernel never called the alarm handler\n");
  }
}

void __attribute__ ((noinline)) foo(int i, int *j) {
  if((i % 2500000) == 0) {
    write(2, ".", 1);
  }
  *j += 1;
}

//
// tests that the kernel calls the handler multiple times.
//
// tests that, when the handler returns, it returns to
// the point in the program where the timer interrupt
// occurred, with all registers holding the same values they
// held when the interrupt occurred.
//
void
test1()
{
  int i;
  int j;

  printf("test1 start\n");
  count = 0;
  j = 0;
  sigalarm(2, periodic);
  for(i = 0; i < 500000000; i++){
    if(count >= 10)
      break;
    foo(i, &j);
  }
  if(count < 10){
    printf("\ntest1 failed: too few calls to the handler\n");
  } else if(i != j){
    // the loop should have called foo() i times, and foo() should
    // have incremented j once per call, so j should equal i.
    // once possible source of errors is that the handler may
    // return somewhere other than where the timer interrupt
    // occurred; another is that that registers may not be
    // restored correctly, causing i or j or the address ofj
    // to get an incorrect value.
    printf("\ntest1 failed: foo() executed fewer times than it was called\n");
  } else {
    printf("test1 passed\n");
  }
}

//
// tests that kernel does not allow reentrant alarm calls.
void
test2()
{
  int i;
  int pid;
  int status;

  printf("test2 start\n");
  if ((pid = fork()) < 0) {
    printf("test2: fork failed\n");
  }
  if (pid == 0) {
    count = 0;
    sigalarm(2, slow_handler);
    for(i = 0; i < 1000*500000; i++){
      if((i % 1000000) == 0)
        write(2, ".", 1);
      if(count > 0)
        break;
    }
    if (count == 0) {
      printf("\ntest2 failed: alarm not called\n");
      exit(1);
    }
    exit(0);
  }
  wait(&status);
  if (status == 0) {
    printf("test2 passed\n");
  }
}

void
slow_handler()
{
  count++;
  printf("alarm!\n");
  if (count > 1) {
    printf("test2 failed: alarm handler called more than once\n");
    exit(1);
  }
  for (int i = 0; i < 1000*500000; i++) {
    asm volatile("nop"); // avoid compiler optimizing away loop
  }
  sigalarm(0, 0);
  sigreturn();
}

// -------------------------------------------------------
// test3: disarm actually stops the handler from firing
// -------------------------------------------------------
void
test3()
{
  int i;
  printf("test3 start\n");
  count = 0;
  sigalarm(2, periodic);

  // let the handler fire at least once
  for(i = 0; i < 500*500000; i++){
    if(count > 0) break;
  }
  if(count == 0){
    printf("\ntest3 failed: handler never fired before disarm\n");
    return;
  }

  // disarm
  sigalarm(0, 0);
  int count_at_disarm = count;

  // spin for a long time — handler must NOT fire again
  for(i = 0; i < 1000*500000; i++){
    asm volatile("nop");
  }

  if(count != count_at_disarm){
    printf("\ntest3 failed: handler fired %d time(s) after sigalarm(0,0)\n",
           count - count_at_disarm);
  } else {
    printf("test3 passed\n");
  }
}

// -------------------------------------------------------
// test4: per-process isolation — two processes have
//        independent alarms that do not interfere
// -------------------------------------------------------
volatile static int parent_fired;
volatile static int child_fired;

void parent_handler() { parent_fired++; sigreturn(); }
void child_handler()  { child_fired++;  sigreturn(); }

void
test4()
{
  int pid, status;
  printf("test4 start\n");

  if((pid = fork()) < 0){
    printf("test4: fork failed\n");
    return;
  }

  if(pid == 0){
    // CHILD: arm with its own handler
    child_fired = 0;
    parent_fired = 0;
    sigalarm(2, child_handler);
    int i;
    for(i = 0; i < 500*500000; i++){
      if(child_fired >= 3) break;
    }
    sigalarm(0, 0);

    if(child_fired == 0){
      printf("\ntest4 failed: child handler never fired\n");
      exit(1);
    }
    if(parent_fired > 0){
      printf("\ntest4 failed: parent handler fired in child process\n");
      exit(1);
    }
    exit(0);
  } else {
    // PARENT: arm with its own handler
    parent_fired = 0;
    child_fired  = 0;
    sigalarm(3, parent_handler);
    int i;
    for(i = 0; i < 500*500000; i++){
      if(parent_fired >= 3) break;
    }
    sigalarm(0, 0);

    wait(&status);

    if(parent_fired == 0){
      printf("\ntest4 failed: parent handler never fired\n");
      return;
    }
    if(child_fired > 0){
      printf("\ntest4 failed: child handler fired in parent process\n");
      return;
    }
    if(status != 0){
      printf("\ntest4 failed: child reported failure\n");
      return;
    }
    printf("test4 passed\n");
  }
}

// -------------------------------------------------------
// test5: child should NOT inherit parent's alarm after fork
// -------------------------------------------------------
volatile static int inherited_count;
void inherit_handler() { inherited_count++; sigreturn(); }

void
test5()
{
  int pid, status;
  printf("test5 start\n");

  // arm alarm in parent BEFORE forking
  inherited_count = 0;
  sigalarm(2, inherit_handler);

  if((pid = fork()) < 0){
    printf("test5: fork failed\n");
    sigalarm(0, 0);
    return;
  }

  if(pid == 0){
    // CHILD: we never called sigalarm — handler must not fire
    inherited_count = 0;
    int i;
    for(i = 0; i < 1000*500000; i++){
      asm volatile("nop");
    }
    if(inherited_count > 0){
      printf("\ntest5 failed: child inherited parent alarm (%d fires)\n",
             inherited_count);
      exit(1);
    }
    exit(0);
  } else {
    wait(&status);
    sigalarm(0, 0);
    if(status != 0){
      printf("\ntest5 failed\n");
    } else {
      printf("test5 passed\n");
    }
  }
}

// -------------------------------------------------------
// test6: handler that makes syscalls (write, uptime)
//        registers must survive a kernel re-entry inside handler
// -------------------------------------------------------
volatile static int syscall_count;

void
syscall_handler()
{
  syscall_count++;
  // make two syscalls from inside the handler
  write(2, "!", 1);
  uptime();
  sigreturn();
}

void __attribute__((noinline)) bar(int i, int *j)
{
  *j += 1;
  (void)i;
}

void
test6()
{
  int i, j;
  printf("test6 start\n");
  syscall_count = 0;
  j = 0;

  sigalarm(2, syscall_handler);
  for(i = 0; i < 500000000; i++){
    if(syscall_count >= 5) break;
    bar(i, &j);
  }
  sigalarm(0, 0);

  if(syscall_count < 5){
    printf("\ntest6 failed: handler fired only %d time(s)\n", syscall_count);
  } else if(i != j){
    printf("\ntest6 failed: register corruption detected (i=%d j=%d)\n", i, j);
  } else {
    printf("test6 passed\n");
  }
}

// -------------------------------------------------------
// test7: handler fires at approximately the right interval
//        uses uptime() to timestamp each firing and checks
//        that gaps between consecutive fires match sigalarm(N)
// -------------------------------------------------------
volatile static int interval_count;
static uint fire_ticks[6];  // timestamp of each handler invocation

void
interval_handler()
{
  if(interval_count < 6)
    fire_ticks[interval_count] = uptime();
  interval_count++;
  sigreturn();
}

void
test7()
{
  int i, ok;
  printf("test7 start\n");
  interval_count = 0;

  uint start = uptime();
  sigalarm(2, interval_handler);

  // wait until handler has fired at least 5 times
  for(i = 0; i < 2000*500000; i++){
    if(interval_count >= 5) break;
  }
  sigalarm(0, 0);

  if(interval_count < 5){
    printf("\ntest7 failed: handler only fired %d time(s)\n", interval_count);
    return;
  }

  ok = 1;

  // check first fire happened roughly N=2 ticks after arm
  uint first_gap = fire_ticks[0] - start;
  if(first_gap < 1 || first_gap > 4){
    printf("\ntest7 failed: first fire was %d ticks after arm (expected ~2)\n",
           first_gap);
    ok = 0;
  }

  // check gaps between consecutive firings are each ~2 ticks
  for(i = 1; i < 5; i++){
    uint gap = fire_ticks[i] - fire_ticks[i-1];
    if(gap < 1 || gap > 4){
      printf("\ntest7 failed: gap between fire %d and %d was %d ticks (expected ~2)\n",
             i-1, i, gap);
      ok = 0;
    }
  }

  if(ok) printf("test7 passed\n");
}