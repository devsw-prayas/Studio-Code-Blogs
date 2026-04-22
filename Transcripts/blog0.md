# The Art of Atomics

## Section 1: CPUs Go BRRR, Threads Go Vroom!

At the heart of whatever device you are holding to read this lies a tiny, delicate piece of silicon (literally), called the CPU.  It doesn’t think, it doesn’t reason. It executes instructions. That’s all it does. Inside it are cores, the units that actually run your code. But here’s where things get interesting: a single core doesn’t just run one stream of instructions. It can run multiple.

That's what we call threads. And they are not separate machines. A thread is simply one such stream of instructions being executed. Even a single core running one program is executing a thread.

On Intel this is called Hyper-Threading, on AMD, Simultaneous Multithreading (SMT). What SMT gives us is the ability to run more than one of these streams on the same core at the same time. They are not two cores. They share the same execution engine, the same pipelines, and the same caches. The hardware is the same — only the architectural state is duplicated. So these threads don’t run independently. They compete.

These threads don't just share execution resources, they also operate on the same memory.

Say, you stored your bank balance in some sneaky variable in RAM. Thread A would update it when you performed a transaction, and Thread B can also read that value (ahem, your account balance). This is how threads communicate, by reading and writing data.

At first glance, it's all sunshine and rainbows. But it isn't.

Even if two threads are running on the same machine, they are not stepping through instructions in lockstep.

One thread might be ahead. Another might be behind. They may observe changes at different times.

There is no single, global timeline that all threads agree on.

## Section 2: Two Threads Walk Into a Bar...

Suppose I opened a company and decided to build a stock exchange for my shares. Naturally, I’d use multiple threads to make it fast. More threads, more performance… right?

Let’s see how that turns out.

Say, all my shares are stored in a variable called *m_TotalShares* storing the total shares I own (450,000 to be exact). An investor wants to buy my shares, she requests 100 shares. A thread is created that is meant to retrieve and verify if the requested amount of shares can be bought. The thread happily waddles along, checks and returns true. No issues whatsoever. She buys my shares.

Now two brothers come to buy some shares, one wants 200,000 the other wants 300,000, but I only have about 450K of them. Both request the shares. You would guess, one of those requests will fail, of course it will fail. But whose request?

Is it brother A, or brother B?

Assuming both threads run concurrently, both requests will be fulfilled.

``` cpp
if(m_TotalShares >= requested) {
    m_TotalShares-=requested; // Subtract the shares
    return true;
}
```

But wait, 300,000 + 200,000 -> 500,000. (Didn't know I had secret shares!). That's more than 450,000, which is wrong. You’d think one of the threads got it wrong.

It didn’t.

Both threads are actually correct *locally*, but wrong *globally*. 

This isn't a rare edge case
The moment two threads read and write the same memory without coordination, this goes wrong

Suppose we have a ticket counter with a variable *m_Counter*. This looks like a simple increment, but it isn't

```
The operation behaves like this
-> Read counter's value
-> Modify the value
-> Write back the value
```

When two threads perform the increment, both read the same value, modify it, and write back. One update is lost.

(One thread basically just got yeeted out of existence.)

Now consider this.

```cpp
transactions++;
balance -= 100;
```

Another thread in the background performs

```cpp
if (transactions > 0)
    print(balance);
```

It is possible that the background thread can swoop in like a superhero and observe the updated transactions, but an old balance, a partial update

We have:

- Lost updates
- Inconsistent reads
- Correct code
- Incorrect results

Nothing here is obviously wrong.

And yet, everything breaks.

So what does "correct" even mean here?

- Should threads see updates immediately?
- Should operations appear in order?
- Should all threads agree on what happened?

Right now, we have no way to answer that.

We need rules.

## Section 3: Maybe this is correct....?

Obviously, we are doing something wrong. Let's try to "engineer" our way through it. 

### [*Experiment 1*]: 

Maybe, since both threads are entering the check at the same time, we need to prevent that from happening!

```cpp
while(busy) {
    // Cutie-patootie thread waiting
}

busy = true; // Thread takes over the world!

if (m_TotalShares >= requested) {
    m_TotalShares -= requested;
}

busy = false; // Thread reconsidered its choices
```

This seems promising. The *while* block prevents any other threads from entering, while only one thread performs a change (if any).

But, wait! The *busy* variable now faces the same issue.
If 2 threads hit the while block at the same time, find it to be false, they can just skip it and perform the change. Great! Back to square one.

[*Experiment 2*]

You know what, let's make our lives a bit easier. *Bye Bye Multithreading*. 

It works perfectly! Only one thread runs and performs the requests. On the other side, all investors can submit their requests and it can be stored in a queue.

```cpp
investorQueue.pushRequest(request);
```

The single worker thread now runs this loop

```cpp
// The chosen one....
while(true){
    auto nextRequest = investorQueue.getNextRequest();

    if(nextRequest.shares <= m_TotalShares){
        m_TotalShares-= nextRequest.shares;
    }
}
```

Yes this is good, we have an amazing solution. But we also lost the biggest advantage of multithreading. Suppose a thousand investors want to buy my shares (I'll be rich!), and each transaction costs a minute of work (We chose a simplified model to prevent mental trauma), then it would take 1000 minutes, or 16.666667 hours. 

Application works, but painfully slow. Those investors are gonna have a real bad time.

[*Experiment 3*]

Alright, let's get our thinking cap on. You might already know what I am about to say. In experiment 2
we used a queue to push requests so the worker thread can poll them and perform them.

But here's the kicker, the queue is a shared structure and is not coordinated with all the threads that might try to access it. We could have 100 threads interfering with each other and corrupting the queue, only because they were so unlucky that they hit the queue at the exact same time.

Lets stop sharing memory (no more of *sharing is caring*). Each thread has its own queue.

```cpp
// N Threads , N Queues. 
// No sharing
motherQueue<RequestQueue>[MAX_THREADS];
```

Now a mother thread will iterate over all queues, and fulfil all requests one by one.

```cpp
while(true){
    for(each : motherQueue){
        while(!each.isEmpty()){
            // Perform the check and transactions.
        }
    }
}
```

But hold on. We made it serialized. Again!
The order in which the mother thread processes queues now decides who gets the shares.

Process A first → B might fail

Process B first → A might fail

Which one is correct? We still don’t know.    

We need some order, some rules, something that guarantees how data is shared safely

Else we’ll be running in circles, fixing concurrent read-writes at 3 am.

## Section 4: Hardware Gods Incarnate: TSO (Total Store Ordering)

Clearly, the Gods above (named x86) blessed us with some rules. A bit too lenient, to be honest.

Let's see what the scriptures say....

Suppose we have 2 variables *A* and *B*.

```cpp
A = 1;
B = 2;
```

The scriptures say that if a thread ran the above code, then for any threads looking at it, this order will be preserved. A is set to 1, and then B is set to 2, no excuses!

Similarly, if I were to set *A* and *B* to two variables *x* and *y*....

```cpp
x = A;
y = B;
```

Again, the order of executing them is preserved.

What if it was this?
```cpp
r = B;
A = 1;
```
Ordering is preserved

Pretty simple, right? Now the weird part.

But if we had this?
```cpp
A = 1;
r = B;
```

You might think that it would preserved, but no.
This is the only exception.
The CPU delays making the store visible to other threads.

So the load can execute before that write is observed by anyone else.

Inside the thread, the order is preserved.

But from the outside, it may appear as if it wasn’t.

Let's look at something interesting that TSO enables

```cpp
// Thread 1
A = 1;
r1 = B;

// Thread 2
B = 1;
r2 = A;
```

Now when both threads run, what do we see?

Thread A finds *r1* to be zero. Similarly, Thread B finds *r2* to be zero.
Both stores are still not visible when the loads happen.

Inside a thread, program order is preserved.

Across threads, what you observe depends on *when* writes become visible.

TSO does not guarantee:

- Immediate visibility of writes
- A single global timeline
- Agreement between threads on the order of events

So the hardware gives us order—but not agreement.

The next question is:

When does a write actually become visible?

## Section 5: I spy with my eye....a stale byte?

Alright so we saw how TSO imposes some interesting (and dumb) rules.
But what about writes? We didn't answer that, did we?

Let's rather look at what happens.

We return back to my stock exchange. I hope you remember.

```cpp
while(busy) {
    // Cutie-patootie thread waiting again!
}

busy = true; // Thread tries taking over the world!

if (m_TotalShares >= requested) {
    m_TotalShares -= requested;
}

busy = false; // Thread reconsidered its choices again!
```

Now suppose two threads are running this same code, trying to evaluate their independent requests, and this time as luck would have it,
one thread flips `busy = true`. 

What happens then?

Each core has a cute little buffer called the *STORE BUFFER!* (Yes, completely original naming). The thread writes its change to the store buffer. But that change is not yet visible to other cores.

What does that mean? 

The other thread may still see it as false. It just blasts past the code, of-course now it's another problem, we have no guarantee when the stock changes performed by the first thread will become visible.

This is called a stale read. A thread observes an old value, even though a newer one already exists.

## Section 6: CPU: "Sir, It's In The Buffer!"

(It's SuperScalar time!!) 
Everything we have seen hints to something important, the CPU doesn't execute sequentially, and even if it does, there's no guarantee that it becomes visible across all cores.

But what lies at the core of the core (pun intended)? 

Modern CPUs are built on Out-of-Order superscalar execution. 
What does that mean?

Instructions are fetched, decoded, and then the CPU looks ahead at what can be executed.

It doesn’t wait.

If an instruction is ready, it fires. Multiple instructions can execute at the same time — out of order. Once executed, results are written to registers or placed into a store buffer.

But here’s the catch.

Just because an instruction executed, doesn’t mean the world can see it.
Instructions *retire* in order, giving us the illusion that everything happened sequentially. But stores? They sit in the store buffer.

Waiting....

Only later do they make their way into the cache and become visible to other cores. The CPU keeps track internally, making sure your thread sees the correct result. But other cores aren't part of that deal.

Now when our thread comes forth to read `busy`, it might still find the same value. The write exists — but only inside the core.
It hasn't left the store buffer yet.

Basically the CPU ran the program in secret and forgot to declare the results to the world.

## Section 7: CPU Lockdown!
Remember when we asked when a write actually becomes visible? here's your answer.

Suppose you caught the poor CPU red-handed, hiding results in its stash (read as *store buffer*). What would you do to make it confess?

Let's drop down to *Assembly* for this (ooo, spicy!)

The x86 Gods blessed us with a really intriguing instruction, called 

```asm
LOCK
```

Some of you are already connecting those dots, keeping going!

`LOCK` is not really an instruction in the usual sense — it's a prefix on an instruction. When prefixed, it produces what we like to call… *a true hardware-level atomic*.

It tells the CPU:

- no other core can interfere with this memory location while the operation is happening  
- the operation must appear as a single, indivisible step to all other threads  

(I’ll be throwing the word *atomic* around a lot!)

But here’s the real power.

`LOCK` doesn’t just make the operation atomic. It forces the CPU to stop hiding things. The operation forces sufficient visibility and ordering across cores. No reordering. No delaying.

When the operation completes, every core sees the result.
`LOCK` basically tells the CPU:
“Enough games. Make it real.”

Enough of my stock exchange though, the company has sunk. Let's open a new business.

I now own a bakery. Let's say all my customers have to book a slot before purchasing. We have a counter *m_SlotCounter* that keeps the current slot count. So to perform a true atomic increment we write....(in assembly of course!)

```asm
lock inc dword ptr [m_SlotCounter] 
```

This instruction simply increments m_SlotCounter, and the lock prefix makes it atomic, globally visible and ordered.

The CPU must complete this operation and make the result visible before moving forward.

What used to be a sweet little load, modify and store has become a true indivisible operation.

But what if we want greater control over it?

We present the *legendary*...

```asm
lock cmpxchg dword ptr [m_SlotCounter], ecx  ; ecx = new value, eax = expected (implicit)
```

It compares the value in memory with the value in `EAX`.

- If they match → it performs the swap with the new value  
- If they don’t → it updates `EAX` with the current value  

All atomically.

If you remember the brothers who came to buy my stocks, you know this is perfect for them. 

What if we don’t want to perform an operation, but just want all writes to become visible? (say abracadabra!).

And thus, we present *FENCES* (another original engineer name!).

**mfence** -> Beep Beep! Lockdown (Forces all prior loads and stores to complete. Nothing crosses this point.).

**sfence** -> May I check your pockets?? (Ensures all previous stores are globally visible before continuing.).

**lfence** -> Search and Report duty! (Ensures all previous loads complete before any new loads proceed.)

Unlike `LOCK`, fences don't perform any operation.
They just enforce order and visibility.

Also, if you haven't guessed it already, `LOCK` implies a full memory barrier on x86 (similar to `mfence`).

That means it not only makes the operation atomic, but also enforces ordering and visibility across all cores.

## Section 8: Pop The Hood

Enough theory. Let’s see what your code actually turns into.
Fire up your compiler. Open the disassembler.

For technicalities...

My system is:
Dell G15 5530, i5-13450HX 10 Cores 16 Threads.

We will be looking Assembly generated by Clang LLVM, (who even loves MASM?)

This time, we are gonna use your business as an example. 
Suppose you opened a car dealership with exclusive cars.

You have a max waitlist of 32 buyers (can I have one too?).

We define a simple function that simply checks if the current waitlist is filled or not, if not then update and swap.

```cpp
#include <atomic>

const int WAIT_LIST = 32;
std::atomic<int> list = {};

void getAQuote() {
    int value = list.load(std::memory_order_seq_cst);

    while (value < WAIT_LIST) {
        if (list.compare_exchange_weak(
            value,
            value + 1,
            std::memory_order_seq_cst,
            std::memory_order_seq_cst
        )) {
            return;
        }
    }
}
```

Oh, you haven't lost it yet?
No worries, here's a heart attack.

```asm
getAQuote():
        mov     eax, dword ptr [rip + list]
        cmp     eax, 31
        jg      .LBB1_3
        lea     ecx, [rax + 1]
        lock    cmpxchg dword ptr [rip + list], ecx
        je      .LBB1_3
        cmp     eax, 32
        jl      .LBB1_1
        ret
```

(Note: This asm is Windows + Clang targeting MSVC ABI — Linux users will see list instead of the mangled symbol, same instructions otherwise.)

Looks terrifying?

It’s not.

This is exactly what we built up to.

Let’s break this down.

### Load current value

```asm
mov eax, dword ptr [list]
```

Load the current value of `list` into `EAX`. This is:

```cpp
value = list.load(...)
```

### Check waitlist limit

```asm
cmp eax, 31
jg  .done
```

Compare against `WAIT_LIST - 1`. If already full -> exit.

### Compute next value

```asm
lea ecx, [rax + 1]
```

Compute the new value (`value + 1`) into `ECX`.
the compiler uses `lea` instead of `mov` + `add` — one instruction instead of two. small optimization, free of charge

### The important line

```asm
lock cmpxchg dword ptr [list], ecx
```

This is your `compare_exchange_weak`. What it does:

* Compare `EAX` with `[list]`
* If equal → write `ECX` into `[list]`
* If not → load actual value into `EAX`

All atomically.

### Check success

```asm
je .done
```

If the swap succeeded → exit.

### Retry condition

```asm
cmp eax, 32
jl  .retry
```

If it failed but still within limit → retry. Notice something subtle:

On failure, `EAX` was updated with the latest value. So we retry with fresh data.

One interesting detail here. (If you caught it, you're a master of dark arts or something!) Clang is being a bit clever.

It has peeled the first iteration of the loop out for efficiency.

Instead of immediately jumping into a loop, it:

- performs the first check upfront  
- attempts the swap once  
- and only then falls back to the retry path  

This is why the control flow looks slightly different from our original code. Compilers don’t just translate code. They restructure it.

No locks.
No blocking.
Just competition and retry.

## Section 9: I hear you, We need to go deeper!

We have looked at many amazing (and painful things). Let's make it easier for you now. 

Before we continue, it's time we truly look at fences. We already tasted them once, it's ready for one more crack at it.

Whether you're a linux freak, or a windows lover (Sweet Memories Windows 7), we'll look at 'em all

Fences come in two flavors. And mixing them up is how you get bugs that only appear in production, on hardware you don't own.

**Hardware Fences** — The CPU bows to the commands! (Direct assembly emissions that enforce the fence behavior)
**Compiler Fences** — The CPU never even sees them! (Poking the compiler so it doesn't optimize too hard! CPU is unaware)

You already met our fence besties in Section 7. They deserve a good introduction, don't they?

If you're on linux, then your compiler gives you access to these
```cpp
__builtin_ia32_mfence(); // mfence — full barrier
__builtin_ia32_sfence(); // sfence — stores only
__builtin_ia32_lfence(); // lfence — loads only
```

And on windows?
We have these

```cpp
_mm_mfence(); // mfence
_mm_sfence(); // sfence
_mm_lfence(); // lfence
```
(Different Passports for different OSes!)

If you want compiler fences, then you have to use these:

```cpp
asm volatile("" ::: "memory"); // Linux!
```

```cpp
_ReadWriteBarrier(); // load+store
_ReadBarrier();      // loads only
_WriteBarrier();     // stores only
```

Also, the devs over at Microsoft deprecated the compiler barriers. People were getting naughty-naughty with them (They thought this is the same as a full hardware fence!).

While we are down here, you should know something, there's a keyword called `volatile`. On windows it gets a superpower, 
`volatile` reads are acquire-like operations and writes become release-like operations.
Cool right?
That means....
```cpp
    volatile int value = 100;

    value = 30;   // release-like behavior
    int b = value; // acquire-like behavior
```

But on linux, this is just normal volatile, no superpowers.

Don't get excited, volatile is not a replacement for atomics, it's a relic from Windows' past

(We'll be looking at what those terms *acquire* and *release* mean in the next section!)

And thus, this gives us two layers of control:

- Compiler fences → stop the compiler from reordering instructions  
- Hardware fences → stop the CPU from reordering execution  

You need both.

The compiler can betray you before execution.
The CPU can betray you during execution.

Fences make both behave.

## Section 10: May the Threads Be Ordered!

We've been throwing around terms like *acquire* and *release* since Section 9. Time to pay the debt.

Memory ordering is not about the order your code is written. It's about the order the CPU and compiler are *allowed* to execute and make visible. And by default? They have a lot of freedom.

Too much freedom.

`std::atomic` gives you the weapon. Memory orders tell it how hard to swing.

### The Weakest Link: `memory_order_relaxed`

No guarantees. None.

The operation is atomic — meaning no torn reads or writes — but the compiler and CPU can reorder it however they please relative to everything else.

```cpp
counter.fetch_add(1, std::memory_order_relaxed);
```

This is fine when you only care that the increment happens atomically, and you don't care when other threads see it, or in what order it happened relative to other operations.

A classic use case — a statistics counter. You want an accurate total eventually. You don't care about ordering.

```cpp
hits.fetch_add(1, std::memory_order_relaxed); // just count it
```

Nothing from our stock exchange or bakery would survive this. But for pure counters? It's the fastest option.

### The Handshake: `acquire` and `release`

Remember the stale read from Section 5? Thread A writes, Thread B reads an old value because the store hadn't left the store buffer yet?

This is exactly what acquire and release fix.
They are often used as a pair. The release publishes data. The acquire consumes it.

Together, they form synchronization. (Sweet-Sweet synchronization!)

**Release** — on a store. "Everything I did before this write is visible to whoever sees this write."

**Acquire** — on a load. "I won't allow reads after this point to move before it."

```cpp
// Thread A
data = 42;                                         // plain store
flag.store(true, std::memory_order_release);       // publish

// Thread B
while (!flag.load(std::memory_order_acquire));     // wait
std::cout << data;                                 // guaranteed to see 42
```

Thread B spinning on `flag` with acquire will not proceed until it sees Thread A's release store. And once it does, it's guaranteed to see `data = 42` too. Not a stale value. Not a partial write.

The release establishes a happens-before relationship.
The acquire observes it.

Go back to Section 2. The brothers buying stocks. If Thread A released the updated share count and Thread B acquired it, Thread B would have seen the correct value before making its decision. The whole mess traced back to this — no synchronization, no happens-before, chaos.

### The Middle Ground: `acq_rel`

Some operations are both a load and a store. Compare-exchange. Fetch-add on shared state you're also reading.

For those, you need both acquire and release at once.

```cpp
list.compare_exchange_weak(
    value,
    value + 1,
    std::memory_order_acq_rel,
    std::memory_order_relaxed
);
```

You saw this in Section 8. The `lock cmpxchg` in the assembly wasn't just atomic — it was carrying ordering guarantees too. `acq_rel` on a read-modify-write operation means:

- the load side gets acquire
- the store side gets release
- everything before it stays before it, everything after stays after it

No full barrier. Just enough.

### The Hammer: `seq_cst`

This is the default. The one you get if you don't specify anything.

Sequential consistency. Every thread agrees on a single global order of all `seq_cst` operations. Not just happens-before edges — a total order.

```cpp
x.store(1, std::memory_order_seq_cst);
r1 = y.load(std::memory_order_seq_cst);
```

Remember Section 4? TSO allowed both threads to read stale values simultaneously — `r1 = 0` and `r2 = 0` at the same time. `seq_cst` prevents that. All threads agree on what happened first.

On x86 this usually maps to `mfence` or the implicit barrier from `lock` prefixed instructions — which you already saw in the assembly output in Section 8. On weaker architectures like ARM, `seq_cst` is significantly more expensive because the hardware gives far fewer guarantees by default.

It's the safest. It's the slowest. And it's what most people reach for when they're not sure.

### Putting It Together

| Order | What it guarantees | Cost |
|---|---|---|
| `relaxed` | Atomicity only | Cheapest |
| `acquire` | Prevents later reads from moving before | Low |
| `release` | Prevents earlier writes from moving after | Low |
| `acq_rel` | Both, for read-modify-write | Medium |
| `seq_cst` | Global total order | Most expensive |


(There’s also `memory_order_consume`. In theory, it’s weaker than acquire. In practice, compilers treat it as acquire. So nobody uses it. We’re skipping it.)

The weakest order that's still correct is the right choice. But correctness first. Always.

Reach for `seq_cst` when in doubt. Profile later. Weaken carefully.

Memory order is not about what your code does.

It’s about what the machine is allowed to get away with.

## Section 11: Around the World in Two Passports

We've seen `std::atomic`. We've seen the assembly it emits. We've seen `lock cmpxchg` do its thing in Section 8.

But before `std::atomic` existed, before the C++11 memory model was even a thing, platforms had their own ways of doing this. And they're still around. You'll see them in legacy codebases, Windows kernel code, and driver land.

Time to meet them properly.

### The Windows Side: Interlocked*

MSVC gives you a family of functions prefixed with `Interlocked`. They live in `<windows.h>` and they are — by definition — full seq_cst operations. No memory order parameter. No choice. Always the hammer.

**InterlockedIncrement**

```cpp
LONG value = 0;
InterlockedIncrement(&value);
```

Atomically increments `value` by 1. Returns the new value. Maps directly to:

```asm
lock inc dword ptr [value]
```

Sound familiar? That's the same instruction from Section 7. `lock inc`. Store buffer drained. Globally visible. Indivisible.

**InterlockedCompareExchange**

```cpp
LONG expected = 5;
LONG newValue  = 10;
LONG previous  = InterlockedCompareExchange(&target, newValue, expected);
```

If `target == expected`, swap in `newValue`. Returns the original value of `target` either way.

Maps to:

```asm
lock cmpxchg dword ptr [target], ecx   ; ecx = newValue, eax = expected
```

Again — Section 7 and Section 8. The same `lock cmpxchg` that `compare_exchange_weak` emits. No surprise there.

The difference? `InterlockedCompareExchange` has no failure memory order. It's always seq_cst on both success and failure. `std::atomic` lets you weaken the failure case — which is exactly what we did in Section 10 with `acq_rel` on success and `relaxed` on failure.

### The Linux Side: `__atomic_*`

GCC and Clang expose the `__atomic_*` builtins. Unlike the Interlocked family, these take a memory order parameter — same orders you know from Section 10.

**__atomic_fetch_add**

```cpp
int value = 0;
__atomic_fetch_add(&value, 1, __ATOMIC_SEQ_CST);
```

Atomically adds 1, returns the old value. With `__ATOMIC_SEQ_CST` this emits:

```asm
lock xadd dword ptr [value], eax
```

`lock xadd` — exchange and add, atomically. Full barrier implied.

Unlike `InterlockedIncrement`, which returns the new value, `__atomic_fetch_add` returns the old value.

**__atomic_compare_exchange_n**

```cpp
int expected = 5;
int desired  = 10;
__atomic_compare_exchange_n(&target, &expected, desired, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
```

Same semantics as `InterlockedCompareExchange`, but with explicit memory orders for success and failure. The `false` indicates a strong compare-exchange (no spurious failures).

Emits:

```asm
lock cmpxchg dword ptr [target], ecx
```

Identical output to the Windows side at seq_cst. The hardware doesn't care which API you used.

### Before `std::atomic`: The `__sync_*` Family

Worth a quick mention. Before `__atomic_*`, GCC had `__sync_*`:

```cpp
__sync_fetch_and_add(&value, 1);
__sync_val_compare_and_swap(&target, expected, desired);
```

Always seq_cst. No memory order parameter. Sound familiar? Same philosophy as Interlocked — full barrier, no choice.

Deprecated now. But you'll see it in old codebases. Don't use it in new code.

### How They Stack Up

| | `Interlocked*` | `__sync_*` | `__atomic_*` | `std::atomic` |
||||||
| Platform | Windows only | Linux/GCC legacy | Linux/GCC/Clang | Cross-platform |
| Memory order control | None (always seq_cst) | None (always seq_cst) | Yes | Yes |
| CompareExchange | `InterlockedCompareExchange` | `__sync_val_compare_and_swap` | `__atomic_compare_exchange_n` | `compare_exchange_weak/strong` |
| Increment | `InterlockedIncrement` | `__sync_fetch_and_add` | `__atomic_fetch_add` | `fetch_add` |
| Assembly emitted | `lock cmpxchg` / `lock inc` | `lock cmpxchg` / `lock xadd` | `lock cmpxchg` / `lock xadd` | Same |
| Use in new code? | Legacy/Windows specific | No | If needed | Yes |

Same silicon. Same instructions. Different spellings.

`std::atomic` is the right choice for new code — portable, expressive, and lets you tune the memory order to exactly what you need. But now you know what's underneath, and you won't be surprised when you meet the others in the wild.

All of these are just different ways of asking the same thing:
“Make this operation atomic, ordered, and visible.”

## Section 12: Time for a Test Drive!

Enough building blocks. Let's put it all together.

We have three patterns. Each one showcases a different slice of everything we've built up across eleven sections. We'll write the C++, look at what two compilers do with it, and understand why.

Let's go.

### Pattern 1: Spinlock

Remember Section 3? That broken `busy` flag?

```cpp
while(busy) {}
busy = true;
```

Two threads hit it simultaneously, both see false, both blast through. Chaos.

Here's what it looks like when you do it correctly:

```cpp
std::atomic<bool> lock = {};

void spinlock_lock() {
    bool expected = false;
    while (!lock.compare_exchange_weak(
        expected,
        true,
        std::memory_order_acquire,
        std::memory_order_relaxed
    )) {
        expected = false;
    }
}

void spinlock_unlock() {
    lock.store(false, std::memory_order_release);
}
```

`compare_exchange_weak` is the fix. Only one thread can win the swap. The loser retries. No two threads can both observe false and proceed — the hardware makes that impossible.

Memory orders: acquire on lock, release on unlock. The classic handshake from Section 10. Everything the winning thread does after locking is visible to the next thread that acquires it.

Now let's see what the compilers say.

**Clang (MSVC ABI):**

```asm
spinlock_lock():
        mov    cl, 1
        xor    eax, eax
        lock   cmpxchg byte ptr [rip + lock], cl
        jne    .LBB1_1
        ret

spinlock_unlock():
        mov    byte ptr [rip + lock], 0
        ret
```

**GCC:**

```asm
spinlock_lock():
        sub    rsp, 24
        mov    edx, 1
        xor    eax, eax
        mov    BYTE PTR 15[rsp], 0
        lock   cmpxchg BYTE PTR lock[rip], dl
        jne    .L5
        add    rsp, 24
        ret

spinlock_unlock():
        mov    BYTE PTR lock[rip], 0
        ret
```

Core is identical — `lock cmpxchg`, `jne` retry, plain `mov` for unlock.

The differences are telling. Clang keeps everything in registers, producing tight code — attempt once, exit clean, retry only on failure. GCC uses a stack slot, likely due to register allocation constraints, and goes straight into the retry path every time without shortcuts. Same result, different personalities. Compilers don't just translate — they have opinions.

One more thing. Both compilers emitted a tight spin with no `pause` instruction inside the loop. In production you'd want:

```cpp
// inside the retry loop
#if defined(_MSC_VER)
    _mm_pause();
#else
    __builtin_ia32_pause();
#endif
```

The `pause` instruction matters here:

- reduces contention on shared execution resources
- prevents pipeline penalties in tight spin loops
- improves behavior under SMT — the hyper-threading we talked about in Section 1



### Pattern 2: Reference Counter

This is what `std::shared_ptr` does under the hood.

```cpp
std::atomic<int> refcount = {};

void addRef() {
    refcount.fetch_add(1, std::memory_order_relaxed);
}

bool release() {
    if (refcount.fetch_sub(1, std::memory_order_release) == 1) {
        std::atomic_thread_fence(std::memory_order_acquire);
        return true;
    }
    return false;
}
```

The memory order reasoning is worth unpacking carefully.

`addRef` is relaxed. You only need the increment to be atomic. The object must already be alive for `addRef` to be called at all — no ordering required, no happens-before needed.

`release` uses release on the decrement. Every thread that drops a reference publishes everything it did to that object before decrementing. The last thread to decrement needs to see all of that.

And here's where `atomic_thread_fence` finally shows up.

When `fetch_sub` returns 1, this thread is the last one. Before it destroys the object, it needs to acquire — see all the writes from every other thread that released before it. The fence establishes that. One fence, covering everything.

**Clang (MSVC ABI):**

```asm
addRef():
        lock   inc dword ptr [rip + refcount]
        ret

release():
        mov    eax, -1
        lock   xadd dword ptr [rip + refcount], eax
        cmp    eax, 1
        jne    .LBB2_2
        cmp    eax, 1
        sete   al
        ret
```

**GCC:**

```asm
addRef():
        lock   add DWORD PTR refcount[rip], 1
        ret

release():
        lock   sub DWORD PTR refcount[rip], 1
        sete   al
        je     .L9
        ret
```

`addRef` — Clang picks `lock inc`, GCC picks `lock add ... 1`. Same operation, different instruction preference. Both correct.

`release` — this is where they diverge meaningfully.

Clang uses `lock xadd` — exchange and add. It atomically adds -1 and loads the old value into `eax`. Then compares old value against 1. If it was 1, this was the last reference.

GCC uses `lock sub` and relies on the zero flag to detect when the result becomes zero. `sete al` captures it. Different approach, same outcome on the happy path.

The duplicate `cmp eax, 1` in Clang's output before `sete al` is a compiler artifact — not redundant logic, just Clang's code generation being slightly mechanical there.

Now — where is `atomic_thread_fence`?

Neither compiler emitted anything for it.

That's not a bug. On x86, `lock`-prefixed instructions provide sufficient ordering guarantees for this pattern. The acquire fence becomes redundant, and the compiler safely omits it.

On ARM, that fence would emit a real `dmb ish` instruction. The code is correct on both architectures. x86 just gets it for free.



### Pattern 3: Once Flag

Double-checked locking. Done correctly this time.

```cpp
std::atomic<int> state = {}; // 0 = unset, 1 = in progress, 2 = done

bool onceFlag_tryAcquire() {
    int expected = 0;
    return state.compare_exchange_strong(
        expected,
        1,
        std::memory_order_acquire,
        std::memory_order_relaxed
    );
}

void onceFlag_complete() {
    state.store(2, std::memory_order_release);
}

void onceFlag_wait() {
    while (state.load(std::memory_order_acquire) != 2);
}
```

Three states. One thread wins the CAS and does the work. Everyone else spins on `wait` until state hits 2. The release on `complete` synchronizes with the acquire on `wait` — the handshake from Section 10, applied here to initialization.

On failure, we don't need ordering guarantees — relaxed is sufficient. Only the thread that wins needs to establish happens-before.

**Clang (MSVC ABI):**

```asm
onceFlag_tryAcquire():
        mov    ecx, 1
        xor    eax, eax
        lock   cmpxchg dword ptr [rip + state], ecx
        sete   al
        ret

onceFlag_complete():
        mov    dword ptr [rip + state], 2
        ret

onceFlag_wait():
        mov    eax, dword ptr [rip + state]
        cmp    eax, 2
        jne    .LBB3_1
        ret
```

**GCC:**

```asm
onceFlag_tryAcquire():
        xor    eax, eax
        mov    edx, 1
        lock   cmpxchg DWORD PTR state[rip], edx
        sete   al
        ret

onceFlag_complete():
        mov    DWORD PTR state[rip], 2
        ret

onceFlag_wait():
        mov    eax, DWORD PTR state[rip]
        cmp    eax, 2
        jne    .L6
        ret
```

Both compilers produced structurally identical output. Only difference — register initialization order in `tryAcquire`. Clang sets up `ecx` first, GCC zeroes `eax` first. Same result.

Three things worth noting:

`complete` is a plain `mov`. On x86, release stores don't require additional instructions — the architecture already enforces the necessary ordering.

`wait` is a plain `mov` in a loop. Acquire load on x86 is equally free. No barrier instruction needed.

And again — no `pause` in the wait loop. Same caveat as the spinlock. In production, you'd want it.



### What Just Happened

Three patterns. Six asm outputs. And the same story every time.

The C++ memory model gives you the vocabulary. `acquire`, `release`, `relaxed`, `seq_cst`. The hardware does the work. On x86, many ordering guarantees come for free due to TSO. Stronger architectures like ARM require explicit barriers.

The `lock` prefix does the heavy lifting. Everything else — fences, memory orders, `atomic_thread_fence` — is either redundant on x86 or handled implicitly by the instruction itself.

Everything you saw here reduces to one primitive:

- Read
- Modify
- Write
- Retry

And sometimes…

`LOCK`

Write it correctly in C++. Let the compiler figure out what the hardware actually needs.

That's the whole game.



### One Last Thing...

You did everything right.

Correct atomics. Correct memory orders. The compiler agrees. The hardware agrees.

And yet — something feels off.

Two independent counters. Two threads. Each hammering their own:

```cpp
struct Counters {
    std::atomic<int> a = {};
    std::atomic<int> b = {};
};

// Thread 1
counters.a.fetch_add(1, std::memory_order_relaxed);

// Thread 2
counters.b.fetch_add(1, std::memory_order_relaxed);
```

No shared data. No contention. Should be fast.

It isn't.

Don't believe us? Run it. Measure it. Come back confused.

We'll find out why in the bonus section.

## Bonus: P.S. CPU: Sir, but what about my sweet cache?

You ran the counter benchmark. You measured it. You came back confused.

Let's fix that.

Before we go, there's one more thing the CPU has been hiding from us. Something we never talked about. Something that's been silently wrecking performance this whole time.

The cache.

### The Cache Line

Your CPU doesn't read memory one byte at a time. That would be painfully slow.

It reads in chunks. 64 bytes at a time. These chunks are called **cache lines**.

When you access a variable, the entire 64-byte block around it gets pulled into the cache. Fast subsequent accesses hit the cache instead of RAM. That's the whole point.

But here's where it gets interesting.

### MESI

Every cache line in your CPU lives in one of four states:

**Modified** — this core changed it. Nobody else has the updated value yet.

**Exclusive** — this core owns it. Clean copy, nobody else is looking.

**Shared** — multiple cores have a copy. All clean, all consistent.

**Invalid** — someone else modified it. This copy is stale. Go fetch it again.

That's MESI. Four states. The hardware tracks all of this automatically, across every core, every cache line, all the time.

When Core A writes to a cache line, it broadcasts to every other core: "your copy is now invalid." They have to go fetch the updated line before they can read or write it again.

This is called a **cache invalidation**.

### Back to the Counters

```cpp
struct Counters {
    std::atomic<int> a = {}; // offset 0
    std::atomic<int> b = {}; // offset 4
};
```

`a` and `b` are 4 bytes each. They sit 4 bytes apart. Both fit comfortably inside the same 64-byte cache line.

Thread 1 hammers `a`. Thread 2 hammers `b`.

From the code's perspective — no sharing. Two independent variables.

From the hardware's perspective — same cache line. Every write from Thread 1 invalidates the line on Thread 2's core. Thread 2 has to re-fetch 64 bytes just to update `b`. Thread 1 does the same. Back and forth. Forever.

Two threads. Zero logical contention. A fistfight in the cache every single cycle.

This is **false sharing**.

### The Fix

```cpp
struct Counters {
    alignas(64) std::atomic<int> a = {};
    alignas(64) std::atomic<int> b = {};
};
```

`alignas(64)` forces each counter onto its own cache line. Thread 1 owns one line. Thread 2 owns another. The hardware stops invalidating. The fistfight ends.

One annotation. The performance difference can be dramatic.

### There's More

MESI is deeper than this. State transitions, snooping protocols, MESIF and MOESI on real hardware, store buffers interacting with cache coherence — there's a full story here we haven't told.

But now you know enough to understand why your correct, carefully ordered, perfectly atomic code was still slow.

The hardware is always doing more than you think.

## Conclusion: The Art of Atomics

We started with a CPU that doesn't think. It just executes.

We watched two brothers bankrupt a stock exchange (Section 2). We tried to engineer our way out of it and failed three times (Section 3). We learned that the hardware has rules — lenient, weird rules — and that stores hide in buffers before the world can see them (Sections 4, 5, 6).

We met `LOCK`. We watched it drain the store buffer and make things real (Section 7).

We opened the disassembler and stopped being afraid of assembly (Section 8).

We learned that fences come in two flavors, that `volatile` is a relic, and that the compiler can betray you before the CPU even gets a chance (Section 9).

We gave threads a vocabulary — `relaxed`, `acquire`, `release`, `seq_cst` — and watched the hardware decide what any of it actually costs (Section 10).

We saw that `Interlocked*` and `__atomic_*` and `std::atomic` all collapse into the same two instructions (Section 11).

We built a spinlock, a reference counter, and a once flag. We watched two compilers argue about stack frames (Section 12).

And then the cache humbled us (Bonus).

The art of atomics isn't about memorizing memory orders. It's about understanding what the hardware is actually doing — and writing code that works with it, not against it.

The machine is always doing more than you think.

Now you know a little more of it.