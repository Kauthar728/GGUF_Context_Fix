# Architecture chat

## User
Let me explain MemUnit. MemUnit is the parent class of the whole memory system. It has an orchestrator inside it that knows what goes where, like a router or a manager. Then you have subclasses under it.

## Assistant
So MemUnit is a persistent memory object that acts as a truth container. The orchestrator routes work to the subclasses. That makes MemUnit the authority for memory operations.

## User
Yes. MemUnit connects to MemBus. MemBus is the memory transport layer that moves memory between MemUnits and the Truth Surface. MemDB is an older name for the same lineage.

## Assistant
Understood. So MemBus connects to MemUnit, and MemBus is part of the memory system. The Truth Surface stores the truth that MemUnit produces.

## User
Exactly. VBSTYLE is the bracket based architecture I use to structure everything. VBSTYLE defines the Dynamic GUI structures. Ghost protects CASOS.

## Assistant
Got it. VBSTYLE is a bracket architecture. Ghost is a protection concept for CASOS, which is your overall system.
