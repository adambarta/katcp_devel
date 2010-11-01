#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "katpriv.h"
#include "katcp.h"

/****************************************************************/

#define STATE_FRESH      0
#define STATE_COMMAND    1
#define STATE_WHITESPACE 2 
#define STATE_ARG        3
#define STATE_TAG        4
#define STATE_ESCAPE     5
#define STATE_DONE       6

/****************************************************************/

static int check_array_parse_katcl(struct katcl_parse *p);

/****************************************************************/

struct katcl_parse *create_parse_katcl()
{
  struct katcl_parse *p;

  p = malloc(sizeof(struct katcl_parse));
  if(p == NULL){
    return NULL;
  }

  p->p_state = STATE_FRESH;

  p->p_buffer = NULL;
  p->p_size = 0;
  p->p_have = 0;

  p->p_refs = 0;

  p->p_tag = (-1);

  p->p_args = NULL;
  p->p_current = NULL;

  p->p_count = 0;
  p->p_got = 0;

  return p;
}

void destroy_parse_katcl(struct katcl_parse *p)
{
  if(p == NULL){
    return;
  }

  if(p->p_buffer){
    free(p->p_buffer);
    p->p_buffer = NULL;
  }
  p->p_size = 0;
  p->p_have = 0;

  if(p->p_args){
    free(p->p_args);
    p->p_args = NULL;
  }
  p->p_current = NULL;
  p->p_count = 0;
  p->p_got = 0;

  p->p_refs = 0;
  p->p_tag = (-1);
}

void clear_parse_katcl(struct katcl_parse *p)
{
  p->p_state = STATE_FRESH;

  p->p_have = 0;
  p->p_used = 0;
  p->p_kept = 0;
  p->p_tag = (-1);

  p->p_got = 0;
  p->p_current = NULL;
}

/******************************************************************/

static int before_add_parse_katcl(struct katcl_parse *p)
{
  if(check_array_parse_katcl(p) < 0){
    return -1;
  }

  p->p_current->a_begin = p->p_kept;
  p->p_current->a_end = p->p_kept;
  p->p_current->a_escapes = 0;

  return 0;
}

static void after_add_parse_katcl(struct katcl_parse *p, unsigned int data, unsigned int escapes)
{
  p->p_current->a_end = p->p_kept + data;
  p->p_current->a_escapes = escapes;

  p->p_buffer[p->p_kept + data] = '\0';

  p->p_kept += data + escapes + 1;

  p->p_have = p->p_kept;
  p->p_used = p->p_kept;

  p->p_got++;

  p->p_current = NULL; /* force crash */
}

static char *request_space_parse_katcl(struct katcl_parse *p, unsigned int amount)
{
  char *tmp;
  unsigned int need;

  need = p->p_kept + amount;
  if(need >= p->p_size){

    need++; /* TODO: could try to guess harder */

    tmp = realloc(p->p_buffer, need);

    if(tmp == NULL){
      return NULL;
    }

    p->p_buffer = tmp;
    p->p_size = need;
  }

  return p->p_buffer + p->p_kept;
}

static int add_unsafe_parse_katcl(struct katcl_parse *p, int flags, char *string)
{
  unsigned int len;
  char *ptr;

  if(before_add_parse_katcl(p) < 0){
    return -1;
  }

  len = strlen(string);

  ptr = request_space_parse_katcl(p, len + 1);
  if(ptr == NULL){
    return -1;
  }
  memcpy(ptr, string, len);

  after_add_parse_katcl(p, len, 0);

  return len;
}

#if 0
static int add_buffer_parse_katcl(struct katcl_parse *p, int flags, char *string)
{
  unsigned int len;
  char *ptr;

  if(before_add_parse_katcl(p) < 0){
    return -1;
  }

  len = strlen(string);

  ptr = request_space_parse_katcl(p, len + 1);
  if(ptr == NULL){
    return -1;
  }
  memcpy(ptr, string, len);

  after_add_parse_katcl(p);

  return len;
}
#endif

/******************************************************************/

#if 0
int add_vargs_parse_katcl(struct katcl_parse *p, int flags, char *fmt, va_list args)
{
  char *tmp, *buffer;
  va_list copy;
  int want, got, x, result;

  got = strlen(fmt) + 16;
#if DEBUG > 1
  fprintf(stderr, "queue vargs: my fmt string is <%s>\n", fmt);
#endif

  /* in an ideal world this would be an insitu copy to save the malloc */
  buffer = NULL;

  for(x = 1; x < 8; x++){ /* paranoid nutter check */
    tmp = realloc(buffer, sizeof(char) * got);
    if(tmp == NULL){
#ifdef DEBUG
      fprintf(stderr, "queue vargs: unable to allocate %d tmp bytes\n", got);
#endif
      free(buffer);
      return -1;
    }

    buffer = tmp;

    va_copy(copy, args);
    want = vsnprintf(buffer, got, fmt, copy);
    va_end(copy);
#if DEBUG > 1
    fprintf(stderr, "queue vargs: printed <%s> (iteration=%d, want=%d, got=%d)\n", buffer, x, want, got);
#endif

    if((want >= 0) && ( want < got)){
      result = queue_buffer_katcl(m, flags, buffer, want);
      free(buffer);
      return result;
    }

    if(want >= got){
      got = want + 1;
    } else {
      /* old style return codes, with x termination check */
      got *= 2;
    }

  }

#ifdef DEBUG
  fprintf(stderr, "queue vargs: sanity failure with %d bytes\n", got);
  abort();
#endif

  return -1;
}

int add_args_parse_katcl(struct katcl_parse *p, int flags, char *fmt, ...)
{
  va_list args;
  int result;

  va_start(args, fmt);

  result = queue_vargs_katcl(m, flags, fmt, args);

  va_end(args);

  return result;
}

int add_string_parse_katcl(struct katcl_parse *p, int flags, char *buffer)
{
  return add_buffer_parse_katcl(p, flags, buffer, strlen(buffer));
}

int add_unsigned_long_parse_katcl(struct katcl_parse *p, int flags, unsigned long v)
{
#define TMP_BUFFER 32
  char buffer[TMP_BUFFER];
  int result;

  result = snprintf(buffer, TMP_BUFFER, "%lu", v);
  if((result <= 0) || (result >= TMP_BUFFER)){
    return -1;
  }

  return add_buffer_parse_katcl(p, flags, buffer, result);
#undef TMP_BUFFER
}

int add_signed_long_parse_katcl(struct katcl_parse *p, int flags, unsigned long v)
{
#define TMP_BUFFER 32
  char buffer[TMP_BUFFER];
  int result;

  result = snprintf(buffer, TMP_BUFFER, "%ld", v);
  if((result <= 0) || (result >= TMP_BUFFER)){
    return -1;
  }

  return queue_buffer_katcl(m, flags, buffer, result);
#undef TMP_BUFFER
}

int add_hex_long_parse_katcl(struct katcl_parse *p, int flags, unsigned long v)
{
#define TMP_BUFFER 32
  char buffer[TMP_BUFFER];
  int result;

  result = snprintf(buffer, TMP_BUFFER, "0x%lx", v);
  if((result <= 0) || (result >= TMP_BUFFER)){
    return -1;
  }

  return queue_buffer_katcl(m, flags, buffer, result);
#undef TMP_BUFFER
}

#ifdef KATCP_USE_FLOATS
int add_double_parse_katcl(struct katcl_parse *p, int flags, double v)
{
#define TMP_BUFFER 32
  char buffer[TMP_BUFFER];
  int result;

  result = snprintf(buffer, TMP_BUFFER, "%e", v);
  if((result <= 0) || (result >= TMP_BUFFER)){
    return -1;
  }

  return add_buffer_parse_katcl(m, flags, buffer, result);
#undef TMP_BUFFER
}
#endif

int add_buffer_parse_katcl(struct katcl_parse *p, int flags, void *buffer, int len)
{
  /* returns greater than zero on success */

  unsigned int newsize, had, want, need, result, i, exclusive;
  char *s, *tmp, v;

  exclusive = KATCP_FLAG_MORE | KATCP_FLAG_LAST;
  if((flags & exclusive) == exclusive){
#ifdef DEBUG
    fprintf(stderr, "queue: usage problem: can not have last and more together\n");
#endif
    return -1;
  }

  if(len < 0){
    return -1;
  }

  s = buffer;
  want = buffer ? len : 0;

  /* extra checks */
  if(flags & KATCP_FLAG_FIRST){
    if(s == NULL){
      return -1;
    }
    if(m->m_complete != 1){
#ifdef DEBUG
      fprintf(stderr, "queue: usage problem: starting new message without having completed previous one");
#endif
      return -1;
    }
    switch(s[0]){
      case KATCP_INFORM  :
      case KATCP_REPLY   :
      case KATCP_REQUEST :
        break;
      default :
#ifdef DEBUG
        fprintf(stderr, "queue: usage problem: start of katcp message does not look valid\n");
#endif
        return -1;
    }
  }

  had = m->m_want;
  need = 1 + ((want > 0) ? (want * 2) : 2); /* a null token requires 2 bytes, all others a maximum of twice the raw value, plus one for trailing stuff */

  if((m->m_want + need) >= m->m_size){
    newsize = m->m_want + need;
    tmp = realloc(m->m_buffer, newsize);
    if(tmp == NULL){
#ifdef DEBUG
      fprintf(stderr, "queue: unable to resize output to <%u> bytes\n", newsize);
#endif
      return -1;
    }
    m->m_buffer = tmp;
    m->m_size = newsize;
  }

#ifdef DEBUG
  fprintf(stderr, "queue: length=%d, want=%d, need=%d, size=%d, had=%d\n", len, want, need, m->m_size, had);
#endif

  for(i = 0; i < want; i++){
    switch(s[i]){
      case  27  : v = 'e';  break;
      case '\n' : v = 'n';  break;
      case '\r' : v = 'r';  break;
      case '\0' : v = '0';  break;
      case '\\' : v = '\\'; break;
      case ' '  : v = '_';  break; 
#if 0
      case ' '  : v = ' ';  break; 
#endif
      case '\t' : v = 't';  break;
      default   : 
        m->m_buffer[m->m_want++] = s[i];
        continue; /* WARNING: restart loop */
    }
    m->m_buffer[m->m_want++] = '\\';
    m->m_buffer[m->m_want++] = v;
  }

  /* check if we are dealing with null args, if so put in null token */
  if(!(flags & KATCP_FLAG_MORE) || (flags & KATCP_FLAG_LAST)){
    if((m->m_want > 0) && (m->m_buffer[m->m_want - 1] == ' ')){ /* WARNING: convoluted heuristic: add a null token if we haven't added anything sofar and more isn't comming */
#ifdef DEBUG
      fprintf(stderr, "queue: warning: empty argument considered bad form\n");
#endif
      m->m_buffer[m->m_want++] = '\\';
      m->m_buffer[m->m_want++] = '@';
    }
  }

  if(flags & KATCP_FLAG_LAST){
    m->m_buffer[m->m_want++] = '\n';
    m->m_complete = 1;
  } else {
    if(!(flags & KATCP_FLAG_MORE)){
      m->m_buffer[m->m_want++] = ' ';
    }
    m->m_complete = 0;
  }

  result = m->m_want - had;

  return result;
}
#endif

/******************************************************************/

unsigned int get_count_parse_katcl(struct katcl_parse *p)
{
  return p->p_got;
}

int get_tag_parse_katcl(struct katcl_parse *p)
{
  return p->p_tag;
}

int is_type_parse_katcl(struct katcl_parse *p, char mode)
{
  if(p->p_got <= 0){
    return 0;
  }

  if(p->p_buffer[p->p_args[0].a_begin] == mode){
    return 1;
  }

  return 0;
}

int is_request_parse_katcl(struct katcl_parse *p)
{
  return is_type_parse_katcl(p, KATCP_REQUEST);
}

int is_reply_parse_katcl(struct katcl_parse *p)
{
  return is_type_parse_katcl(p, KATCP_REPLY);
}

int is_inform_parse_katcl(struct katcl_parse *p)
{
  return is_type_parse_katcl(p, KATCP_INFORM);
}

int is_null_parse_katcl(struct katcl_parse *p, unsigned int index)
{
  if(index >= p->p_got){
    return 1;
  } 

  if(p->p_args[index].a_begin >= p->p_args[index].a_end){
    return 1;
  }

  return 0;
}

char *get_string_parse_katcl(struct katcl_parse *p, unsigned int index)
{
  if(index >= p->p_got){
    return NULL;
  }

  if(p->p_args[index].a_begin >= p->p_args[index].a_end){
    return NULL;
  }

  return p->p_buffer + p->p_args[index].a_begin;
}

char *copy_string_parse_katcl(struct katcl_parse *p, unsigned int index)
{
  char *ptr;

  ptr = get_string_parse_katcl(p, index);
  if(ptr){
    return strdup(ptr);
  } else {
    return NULL;
  }
}

unsigned long get_unsigned_long_parse_katcl(struct katcl_parse *p, unsigned int index)
{
  unsigned long value;

  if(index >= p->p_got){
    return 0;
  } 

  value = strtoul(p->p_buffer + p->p_args[index].a_begin, NULL, 0);

  return value;
}

#ifdef KATCP_USE_FLOATS
double get_double_parse_katcl(struct katcl_parse *p, unsigned int index)
{
  double value;

  if(index >= p->p_got){
    return 0;
  } 

  value = strtod(p->p_buffer + p->p_args[index].a_begin, NULL);

  return value;
}
#endif

unsigned int get_buffer_parse_katcl(struct katcl_parse *p, unsigned int index, void *buffer, unsigned int size)
{
  unsigned int got, done;

  if(index >= p->p_got){
    return 0;
  } 

  got = p->p_args[index].a_end - p->p_args[index].a_begin;
  done = (got > size) ? size : got;

  if(buffer && (got > 0)){
    memcpy(buffer, p->p_buffer + p->p_args[index].a_begin, done);
  }

  return got;
}

/************************************************************************************/



/************************************************************************************/

static int check_array_parse_katcl(struct katcl_parse *p)
{
  struct katcl_larg *tmp;

  if(p->p_got < p->p_count){
    p->p_current = &(p->p_args[p->p_got]);
    return 0;
  }

  tmp = realloc(p->p_args, sizeof(struct katcl_larg) * (p->p_count + KATCP_ARGS_INC));
  if(tmp == NULL){
    return -1;
  }

  p->p_args = tmp;
  p->p_count += KATCP_ARGS_INC;
  p->p_current = &(p->p_args[p->p_got]);

  return 0;
}

static int stash_remainder_parse_katcl(struct katcl_parse *p, char *buffer, unsigned int len)
{
  char *tmp;
  unsigned int need;

#ifdef DEBUG
  fprintf(stderr, "stashing %u bytes starting with <%c...> for next line\n", len, buffer[0]);
#endif
  
  need = p->p_used + len;

  if(need > p->p_size){
    tmp = realloc(p->p_buffer, need);
    if(tmp == NULL){
      return -1;
    }
    p->p_buffer = tmp;
    p->p_size = need;
  }

  memcpy(p->p_buffer + p->p_used, buffer, len);
  p->p_have = need;

  return 0;
}

int parse_katcl(struct katcl_line *l) /* transform buffer -> args */
{
  int increment;
  struct katcl_parse *p;

  p = l->l_next;

#ifdef DEBUG
  fprintf(stderr, "invoking parse (state=%d, have=%d, used=%d, kept=%d)\n", p->p_state, p->p_have, p->p_used, p->p_kept);
#endif

  while((p->p_used < p->p_have) && (p->p_state != STATE_DONE)){

    increment = 0; /* what to do to keep */

#if DEBUG > 1
    fprintf(stderr, "parse: state=%d, char=%c\n", p->p_state, p->p_buffer[p->p_used]);
#endif

    switch(p->p_state){

      case STATE_FRESH : 
        switch(p->p_buffer[p->p_used]){
          case '#' :
          case '!' :
          case '?' :
            if(check_array_parse_katcl(p) < 0){
              l->l_error = ENOMEM;
              return -1;
            }

#ifdef DEBUG
            if(p->p_kept != p->p_used){
              fprintf(stderr, "logic issue: kept=%d != used=%d\n", p->p_kept, p->p_used);
            }
#endif
            p->p_current->a_begin = p->p_kept;
            p->p_current->a_escapes = 0;
            p->p_state = STATE_COMMAND;

            p->p_buffer[p->p_kept] = p->p_buffer[p->p_used];
            increment = 1;
            break;

          default :
            break;
        }
        break;

      case STATE_COMMAND :
        increment = 1;
        switch(p->p_buffer[p->p_used]){
          case '[' : 
            p->p_tag = 0;
            p->p_buffer[p->p_kept] = '\0';
            p->p_current->a_end = p->p_kept;
            p->p_got++;
            p->p_state = STATE_TAG;
            break;

          case ' '  :
          case '\t' :
            p->p_buffer[p->p_kept] = '\0';
            p->p_current->a_end = p->p_kept;
            p->p_got++;
            p->p_state = STATE_WHITESPACE;
            break;

          case '\n' :
          case '\r' :
            p->p_buffer[p->p_kept] = '\0';
            p->p_current->a_end = p->p_kept;
            p->p_got++;
            p->p_state = STATE_DONE;
            break;

          default   :
            p->p_buffer[p->p_kept] = p->p_buffer[p->p_used];
            break;
            
        }
        break;

      case STATE_TAG : 
        switch(p->p_buffer[p->p_used]){
          case '0' :
          case '1' :
          case '2' :
          case '3' :
          case '4' :
          case '5' :
          case '6' :
          case '7' :
          case '8' :
          case '9' :
            p->p_tag = (p->p_tag * 10) + (p->p_buffer[p->p_used] - '0');
            break;
          case ']' : 
#if DEBUG > 1
            fprintf(stderr, "extract: found tag %d\n", p->p_tag);
#endif
            p->p_state = STATE_WHITESPACE;
            break;
          default :
#ifdef DEBUG
            fprintf(stderr, "extract: invalid tag char %c\n", p->p_buffer[p->p_used]);
#endif
            /* WARNING: error handling needs to be improved here */
            l->l_error = EINVAL;
            return -1;
        }
        break;

      case STATE_WHITESPACE : 
        switch(p->p_buffer[p->p_used]){
          case ' '  :
          case '\t' :
            break;
          case '\n' :
          case '\r' :
            p->p_state = STATE_DONE;
            break;
          case '\\' :
            if(check_array_parse_katcl(p) < 0){
              l->l_error = ENOMEM;
              return -1;
            }
            p->p_current->a_begin = p->p_kept; /* token begins with an escape */
            p->p_current->a_escapes = 0;
            p->p_state = STATE_ESCAPE;
            break;
          default   :
            if(check_array_parse_katcl(p) < 0){
              l->l_error = ENOMEM;
              return -1;
            }
            p->p_current->a_begin = p->p_kept; /* token begins with a normal char */
            p->p_current->a_escapes = 0;
            p->p_buffer[p->p_kept] = p->p_buffer[p->p_used];
            increment = 1;
            p->p_state = STATE_ARG;
            break;
        }
        break;

      case STATE_ARG :
        increment = 1;
#ifdef DEBUG
        if(p->p_current == NULL){
          fprintf(stderr, "parse logic failure: entered arg state without having allocated entry\n");
        }
#endif
        switch(p->p_buffer[p->p_used]){
          case ' '  :
          case '\t' :
            p->p_buffer[p->p_kept] = '\0';
            p->p_current->a_end = p->p_kept;
            p->p_got++;
            p->p_state = STATE_WHITESPACE;
            break;

          case '\n' :
          case '\r' :
            p->p_buffer[p->p_kept] = '\0';
            p->p_current->a_end = p->p_kept;
            p->p_got++;
            p->p_state = STATE_DONE;
            break;
            
          case '\\' :
            p->p_state = STATE_ESCAPE;
            increment = 0;
            break;

          default   :
            p->p_buffer[p->p_kept] = p->p_buffer[p->p_used];
            break;
        }
        
        break;

      case STATE_ESCAPE :
        increment = 1;
        switch(p->p_buffer[p->p_used]){
          case 'e' :
            p->p_buffer[p->p_kept] = 27;
            break;
          case 'n' :
            p->p_buffer[p->p_kept] = '\n';
            break;
          case 'r' :
            p->p_buffer[p->p_kept] = '\r';
            break;
          case 't' :
            p->p_buffer[p->p_kept] = '\t';
            break;
          case '0' :
            p->p_buffer[p->p_kept] = '\0';
            break;
          case '_' :
            p->p_buffer[p->p_kept] = ' ';
            break;
          case '@' :
            increment = 0;
            break;
            /* case ' ' : */
            /* case '\\' : */
          default :
            p->p_buffer[p->p_kept] = p->p_buffer[p->p_used];
            break;
        }
        p->p_current->a_escapes += increment;
        p->p_state = STATE_ARG;
        break;
    }

    p->p_used++;
    p->p_kept += increment;
  }

  if(p->p_state != STATE_DONE){ /* not finished, run again later */
    return 0; 
  }

  /* got a new line, move it to ready position */

  if(l->l_spare == NULL){
    l->l_spare = create_parse_katcl();
    if(l->l_spare == NULL){
      l->l_error = ENOMEM;
      return -1; /* is it safe to call parse with a next which is DONE ? */
    }
  } else {
    clear_parse_katcl(l->l_spare);
  }

  /* at this point we have a cleared spare which we can make the next entry */
  l->l_next = l->l_spare;

  /* move ready out of the way */
  if(l->l_ready){
#ifdef DEBUG
    fprintf(stderr, "unsual: expected ready to be cleared before attempting a new parse\n");
#endif
    clear_parse_katcl(l->l_ready);
    l->l_spare = l->l_ready;
  } else {
    l->l_spare = NULL;
  }

  /* stash any unparsed io for the next round */
  if(p->p_used < p->p_have){
    stash_remainder_parse_katcl(l->l_next, p->p_buffer + p->p_used, p->p_have - p->p_used);
    p->p_have = p->p_used;
  }

  l->l_ready = p;

  return 1;
}

