#include "userprog/exception.h"
#include <inttypes.h>
#include <stdio.h>
#include "userprog/gdt.h"
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "userprog/syscall.h"

/* Numar de defectiuni pe pagina procesate. */
static long long page_fault_cnt;

static void kill (struct intr_frame *);
static void page_fault (struct intr_frame *);

/* Registrele de manipulare pentru intreruperi care pot fi cauzate 
   programele utilizatorului

   Intr-un sistem de operare de tip UNIX,majoritate acelor intreruperi
   ar fi transmise catre procesele utilizatorului in forma semnale,
   dar nu implementam semnale. In schimb, vom face sa termine sfarseasca 
   utilizatorului. 

   Defectiunile pagini sunt o exceptie.  Aici sunt tratate asemanator 
   celorlalte exceptii, dar asta trebuie sa se schimbe ca sa implementam
   memoria virtuala. */
void
exception_init (void) 
{
  /* Acete exceptii pot fi ridicate explicit de un program al utilizatorului,
     un exemplu ar fi folosind INT, INT3, INTO, si instructiuni BOUND.  
  intr_register_int (3, 3, INTR_ON, kill, "#BP Breakpoint Exception");
  intr_register_int (4, 3, INTR_ON, kill, "#OF Overflow Exception");
  intr_register_int (5, 3, INTR_ON, kill,
                     "#BR BOUND Range Exceeded Exception");

  /* Aceste exceptii au DPL==0, care previn invocarea proceselor utilizatorului
     folosind insctructiunea INT. Dar ele tot pot fi cauzate in mod indirect,
     de exemplu impartind #DE la 0;  */
  intr_register_int (0, 0, INTR_ON, kill, "#DE Divide Error");
  intr_register_int (1, 0, INTR_ON, kill, "#DB Debug Exception");
  intr_register_int (6, 0, INTR_ON, kill, "#UD Invalid Opcode Exception");
  intr_register_int (7, 0, INTR_ON, kill,
                     "#NM Device Not Available Exception");
  intr_register_int (11, 0, INTR_ON, kill, "#NP Segment Not Present");
  intr_register_int (12, 0, INTR_ON, kill, "#SS Stack Fault Exception");
  intr_register_int (13, 0, INTR_ON, kill, "#GP General Protection Exception");
  intr_register_int (16, 0, INTR_ON, kill, "#MF x87 FPU Floating-Point Error");
  intr_register_int (19, 0, INTR_ON, kill,
                     "#XF SIMD Floating-Point Exception");

  /* Majoritatea exceptiilor pot fi manipulate cu optiunea de intreruperi.
     Trebuie sa dezactivam aceasta optiune pentru defectiunile paginilor,
     deoarece defectiunile adreselor sunt stocate in CR2 si trebuie prezervate. */
  intr_register_int (14, 0, INTR_OFF, page_fault, "#PF Page-Fault Exception");
}

/* Afiseaza statistica exceptiilor. */
void
exception_print_stats (void) 
{
  printf ("Exception: %lld page faults\n", page_fault_cnt);
}

/* Manipulator de exceptie cauzat de un program de utilizator. */
static void
kill (struct intr_frame *f) 
{
  /* Aceasta intrerupere este cauzata de un proces de utilizator.
     Pentru moment, trebuie sa incetam procesul utilizatorului.
     Pe urma, vom manipula defectiunile paginilor in kernel.
     Sistemele de operare de tip UNIX trec aproape peste fiecare
     exceptie spre procese prin semnale, dar nu le implementam. */
     
  /* Valoarea segmentului de cod al cadrului de intreruperene spune unde exceptia a luat loc */
  switch (f->cs)
    {
    case SEL_UCSEG:
      /* Segmentul de cod al utilizatorului, este o super-exceptie.
         Incetam procesul utilizatorului.  */
      printf ("%s: dying due to interrupt %#04x (%s).\n",
              thread_name (), f->vec_no, intr_name (f->vec_no));
      intr_dump_frame (f);
      thread_exit (); 

    case SEL_KCSEG:
      /* Segmentul de cod Kernel,care indica un bug kernel.
         Codul kernel nu ar trebui sa aiba exceptii.    */
      intr_dump_frame (f);
      PANIC ("Kernel bug - unexpected interrupt in kernel"); 

    default:
      
      printf ("Interrupt %#04x (%s) in unknown segment %04x\n",
             f->vec_no, intr_name (f->vec_no), f->cs);
      thread_exit ();
    }
}

/* Manipulator de defectiuni a paginii.  Este un schelet care trebuie completat pentru
   implementarea memoriei virtuale.

   Urmatorul exemplu de cod ne arata cum analizam informatia.  */
static void
page_fault (struct intr_frame *f) 
{
  bool not_present;  /* True: not-present page, false: writing r/o page. */
  bool write;        /* True: access was write, false: access was read. */
  bool user;         /* True: access by user, false: access by kernel. */
  void *fault_addr;  /* Fault address. */

  /* Optinem adrese de defectare, adresa virtuala care a fost accesata din 
     cauza defectiunilor. Nu e necesara adresa insctructiuniii care a cauzat 
     defectiunea.. */
  asm ("movl %%cr2, %0" : "=r" (fault_addr));

  /* Pornim din nou intreruperile (au fost oprite doar pentru a ne asigura
     de citirea CR2-ului inainte ca acesta sa se schimbe). */
  intr_enable ();

  /* Numaram defectiunile paginii. */
  page_fault_cnt++;

  //------- Solutie------
  if (!not_present)
    exit(-1);

  if (fault_addr == NULL || !is_user_vaddr(fault_addr) 
    || !pagedir_get_page(thread_current()->pagedir, fault_addr))
    exit(-1);
  //------- Solutie------


  /* Determinam cauza. */
  not_present = (f->error_code & PF_P) == 0;
  write = (f->error_code & PF_W) != 0;
  user = (f->error_code & PF_U) != 0;

  /* Pentru a implementa memoria virtuala, stergem restul functiei body-ului si o 
     inlocuim cu codul care ne aduce pagina care se refera la fault_addr. */
  printf ("Page fault at %p: %s error %s page in %s context.\n",
          fault_addr,
          not_present ? "not present" : "rights violation",
          write ? "writing" : "reading",
          user ? "user" : "kernel");
  kill (f);
}

