#ifndef __INDEX_H__
#define __INDEX_H__

#include "doc_table.h"
#include "forward_index.h"
#include "index_result.h"
#include "score_index.h"
#include "skip_index.h"
#include "types.h"
#include "util/logging.h"
#include "util/heap.h"
#include "varint.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
// /* An IndexResult is the data structure used when reading indexes.
// Each hit representsa single document entry in an inverted index */
// typedef struct {
//   t_docId docId;
//   double totalFreq;
//   u_char flags;
//   VarintVector offsetVecs[MAX_INTERSECT_WORDS];
//   int numOffsetVecs;
//   int hasMetadata;
//   DocumentMetadata metadata;
//   HitType type;
// } IndexResult;

/* Free the internal data of an index hit. Since index hits are usually on the
stack,
this does not actually free the hit itself */
void IndexResult_Terminate(IndexResult *h);

/** Load document metadata for an index hit, marking it as having metadata.
Currently has no effect due to performance issues */
int IndexResult_LoadMetadata(IndexResult *h, DocTable *dt);

#pragma pack(4)
/** The header of an inverted index record */
typedef struct indexHeader {
  t_offset size;
  t_docId lastId;
  u_int32_t numDocs;
} IndexHeader;
#pragma pack()

/* An IndexReader wraps an inverted index record for reading and iteration */
typedef struct indexReader {
  // the underlying data buffer
  Buffer *buf;
  // the header - we read into it from the buffer
  IndexHeader header;
  // last docId, used for delta encoding/decoding
  t_docId lastId;
  // skip index. If not null and is needed, will be used for intersects
  SkipIndex *skipIdx;
  u_int skipIdxPos;
  DocTable *docTable;
  // in single word mode, we use the score index to get the top docs directly,
  // and do not load the skip index.
  int singleWordMode;
  ScoreIndex *scoreIndex;
  int useScoreIndex;
  u_char fieldMask;

  IndexRecord currentRecord;
  IndexFlags flags;
  size_t len;

  Term *term;
} IndexReader;

/* An IndexWriter writes forward index entries to an index buffer */
typedef struct indexWriter {
  BufferWriter bw;
  // last id for delta encoding
  t_docId lastId;
  // the number of documents encoded
  u_int32_t ndocs;
  // writer for the skip index
  BufferWriter skipIndexWriter;
  // writer for the score index
  ScoreIndexWriter scoreWriter;

  IndexFlags flags;
} IndexWriter;

#define INDEXREAD_EOF 0
#define INDEXREAD_OK 1
#define INDEXREAD_NOTFOUND 2

#define TOTALDOCS_PLACEHOLDER (double)10000000
double tfidf(float freq, u_int32_t docFreq);

/* An abstract interface used by readers / intersectors / unioners etc.
Basically query execution creates a tree of iterators that activate each other
recursively */
typedef struct indexIterator {
  void *ctx;
  /* Read the next entry from the iterator, into hit *e.
  *  Returns INDEXREAD_EOF if at the end */
  int (*ReadNext)(void *ctx, IndexResult *e);

  int (*ReadCurrent)(void *ctx, IndexResult *e);

  /* Skip to a docid, potentially reading the entry into hit, if the docId
   * matches */
  int (*SkipTo)(void *ctx, u_int32_t docId, IndexResult *hit);

  /* the last docId read */
  t_docId (*LastDocId)(void *ctx);

  /* can we continue iteration? */
  int (*HasNext)(void *ctx);

  /* release the iterator's context and free everything needed */
  void (*Free)(struct indexIterator *self);

  size_t (*EstimateCardinality)(void *ctx);

  /* Return the number of results in this iterator. Used by the query execution
   * on the top iterator */
  size_t (*Len)(void *ctx);
} IndexIterator;

/* Basic function that just allocates an iterator and zeros all its fields */
IndexIterator *NewIndexIterator(void *ctx);

/* Free a union iterator */
void UnionIterator_Free(IndexIterator *it);

/* Free an intersect iterator */
void IntersectIterator_Free(IndexIterator *it);

/* Free a read iterator */
void ReadIterator_Free(IndexIterator *it);

// used only internally for unit testing
IndexReader *NewIndexReader(void *data, size_t datalen, SkipIndex *si, DocTable *docTable,
                            int singleWordMode, u_char fieldMask, IndexFlags flags);

/* Create a new index reader on an inverted index buffer,
* optionally with a skip index, docTable and scoreIndex.
* If singleWordMode is set to 1, we ignore the skip index and use the score
* index.
*/
IndexReader *NewIndexReaderBuf(Buffer *buf, SkipIndex *si, DocTable *docTable, int singleWordMode,
                               ScoreIndex *sci, u_char fieldMask, IndexFlags flags, Term *term);
/* free an index reader */
void IR_Free(IndexReader *ir);

/* Read an entry from an inverted index */
int IR_GenericRead(IndexReader *ir, t_docId *docId, float *freq, u_char *flags,
                   VarintVector *offsets);

int IR_TryRead(IndexReader *ir, t_docId *docId, t_docId expectedDocId);

/* Read an entry from an inverted index into IndexResult */
int IR_ReadNext(void *ctx, IndexResult *e);

int IR_ReadCurrent(void *ctx, IndexResult *e);
size_t IR_EstimateCardinality(void *ctx);
/* Can we read more from an index reader? */
int IR_HasNext(void *ctx);

/* Skip to a specific docId in a reader,using the skip index, and read the entry
 * there */
int IR_SkipTo(void *ctx, u_int32_t docId, IndexResult *hit);

size_t IR_EstimateCardinality(void *ctx);
/* The number of docs in an inverted index entry */
size_t IR_NumDocs(void *ctx);

/* LastDocId of an inverted index stateful reader */
t_docId IR_LastDocId(void *ctx);

/* Seek the inverted index reader to a specific offset and set the last docId */
void IR_Seek(IndexReader *ir, t_offset offset, t_docId docId);

// void IW_MakeSkipIndex(IndexWriter *iw, Buffer *b);
int indexReadHeader(Buffer *b, IndexHeader *h);

/* Create a reader iterator that iterates an inverted index record */
IndexIterator *NewReadIterator(IndexReader *ir);

/* Close an indexWriter */
size_t IW_Close(IndexWriter *w);

/* Write a ForwardIndexEntry into an indexWriter, updating its score and skip
 * indexes if needed.
 * Returns the number of bytes written to the index */
size_t IW_WriteEntry(IndexWriter *w, ForwardIndexEntry *ent);

/* Get the len of the index writer's buffer */
size_t IW_Len(IndexWriter *w);

/** Free the index writer and underlying data structures */
void IW_Free(IndexWriter *w);
/* Create a new index writer with a memory buffer of a given capacity.
NOTE: this is used for testing only */
IndexWriter *NewIndexWriter(size_t cap, IndexFlags flags);

/* Create a new index writer with the given buffers for the actual index, skip
 * index, and score
 * index */
IndexWriter *NewIndexWriterBuf(BufferWriter bw, BufferWriter skipIndexWriter,
                               ScoreIndexWriter scoreWriter, IndexFlags flags);

/* UnionContext is used during the running of a union iterator */
typedef struct {
  heap_t *iters;
  int num;
  int pos;
  size_t len;
  IndexResult current;
  t_docId lastDocId;
  DocTable *docTable;
  int atEnd;
} UnionContext;

/* Create a new UnionIterator over a list of underlying child iterators.
It will return each document of the underlying iterators, exactly once */
IndexIterator *NewUnionIterator(IndexIterator **its, int num, DocTable *t);

int UI_SkipTo(void *ctx, u_int32_t docId, IndexResult *hit);
int UI_ReadNext(void *ctx, IndexResult *hit);
int UI_ReadCurrent(void *ctx, IndexResult *hit);
size_t UI_EstimateCardinality(void *ctx);
int UI_HasNext(void *ctx);
size_t UI_Len(void *ctx);
t_docId UI_LastDocId(void *ctx);

/* The context used by the intersection methods during iterating an intersect
 * iterator */
typedef struct {
  IndexIterator **its;
  int num;
  size_t len;
  int exact;
  t_docId lastDocId;
  IndexResult current;
  DocTable *docTable;
  u_char fieldMask;
  int atEnd;
} IntersectContext;

/* Create a new intersect iterator over the given list of child iterators. If
exact is one
we will only yield results that are exact matches */
IndexIterator *NewIntersecIterator(IndexIterator **its, int num, int exact, DocTable *t,
                                   u_char fieldMask);
int II_SkipTo(void *ctx, u_int32_t docId, IndexResult *hit);
int II_Next(void *ctx);
int II_Read(void *ctx, IndexResult *hit);
int II_ReadCurrent(void *ctx, IndexResult *hit);

int II_HasNext(void *ctx);
size_t II_Len(void *ctx);
t_docId II_LastDocId(void *ctx);

#endif