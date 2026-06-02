# Retrieval discussion

You said:
The Truth Surface is the authoritative store of meaning assembled from many chats. The Truth Surface depends on MemUnit. I want retrieval that returns meaning, not just text.

ChatGPT said:
A retrieval system uses embeddings to find candidates, then the database returns the meaning. The embedding model is the finder, the database is the truth. MemBus uses the Truth Surface to move results around.

You said:
Right. ConfigSetup validates startup. BootResolver starts initialization. BootResolver uses ConfigSetup. ConfigSetup connects to DatabaseManager.

ChatGPT said:
So BootResolver depends on ConfigSetup, and ConfigSetup creates the DatabaseManager. DatabaseManager is the authority for database initialization.

You said:
The Magnetic Ranking engine finds a word and works its way out to similar words, then groups them and retrieves results. Magnetic Ranking is part of retrieval.

ChatGPT said:
Magnetic Ranking is a similarity ranking concept. It relates to embeddings and to the Truth Surface because both help retrieval.
