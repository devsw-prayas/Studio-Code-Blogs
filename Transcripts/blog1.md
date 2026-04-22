# Criss Cross Crash: Part 1

## Section 1: I like my *PI* to 300 digits!

"Let us assume PI = 3"

 -- *every mechanical engineer ever*

Criminally incorrect. Morally wrong. And somehow load-bearing in real infrastructure.

And I, for reasons that will very quickly become a hardware problem, want 300 digits of it.

"But Prayas, my computer already has double precision. That's like 15 digits."

Yeah. Not enough.

Normally the modern day compiler standards and ISO C++ don’t give you arbitrary precision (Thank God they finished C++ 26 in 2026 tho!)

So, what do we do? Let's make our own high precision number that could help us store enough digits of PI to keep me happy.

P.S. At 300 digits, a single integer won't fit in any register the CPU has. So we break it apart.

## Section 2: Are we cooking yet?
Multi-limb addition. (Whatever that means!)

That’s today’s problem. Multiplication and division?

Not today.
Those are… more emotional experiences. (*The chef is crying.*)

Before we dive into the tidbits, let's understand what multi-limb arithmetic is

When we have a gigantic number, it becomes harder to store the full number at once in a single register.

(No name calling `ymm` and `zmm` pls, I beg you)
So we split the number into **limbs**, 64 bit unsigned integers, and store them preferably as arrays of `uint64_t`

Each limb holds a chunk of the number, ordered from least to most significant.

Something like...

```bash
    0xLIMB_A 0xLIMB_B 0xLIMB_C 0xLIMB_D .....
```
Here `LIMB_*` is a 64-bit unsigned number

For reference, this is PI in hexa-float format at 1024 bit precision

```
0x1.921fb54442d18469898cc51701b839a252049c1114cf98e804177d4c76273644a29410f31c6809bbdf2a33679a748636605614dbe4be286e9fc26adadaa3848bc90b6aecc4bcfd8de89885d34c6fdad617feb96de80d6fdbdc70d7f6b5133f4b5d3e4822f8963fcc9250cca3d9c8b67b8400f97142c77e0b31b4906cp+1
```

We can't store this directly.
So we convert it into a large integer by scaling it, and then split it into limbs.

```
0x0000000000000003 0x243f6a8885a308d3 0x13198a2e03707344  0xa4093822299f31d0 0x082efa98ec4e6c89....
```
The first limb holds the integer part, and the rest store the fractional precision as bits packed into the integer.

Let’s try adding two of these monsters.

The first 