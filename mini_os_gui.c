
/*
 * ============================================================
 *  CS 225: Introduction to Operating Systems
 *  ASSIGNMENT 1 - Mini Operating System (GTK3 GUI Version)
 *  Smart Emergency Response Center (SERC)
 *
 *  Compile with:
 *  gcc mini_os_gui.c -o mini_os_gui.exe $(pkg-config --cflags --libs gtk+-3.0)
 *
 *  Components Implemented:
 *   1. Process Management  - PCB, 5 states, create/view/terminate
 *   2. CPU Scheduling      - FCFS, SJF, Priority
 *   3. Memory Management   - First Fit, coalescing, fragmentation
 *   4. File Management     - Timestamped event logging
 * ============================================================
 */

#include <gtk/gtk.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ============================================================
 *  DATA STRUCTURES
 * ============================================================ */
typedef enum {
    NEW, READY, RUNNING, WAITING, TERMINATED
} ProcessState;

typedef struct PCB {
    int pid;
    char name[30];
    int priority;
    int burst_time;
    int remaining_time;
    int arrival_time;
    int waiting_time;
    int turnaround_time;
    ProcessState state;
    int memory_required;
    int memory_address;
    int sim_id;          /* which simulation this process belongs to */
    struct PCB* next;
} PCB;

typedef struct MemoryBlock {
    int start_address;
    int size;
    int is_free;
    int process_id;
    struct MemoryBlock* next;
} MemoryBlock;

/* ============================================================
 *  GLOBAL STATE
 * ============================================================ */
#define MAX_PROCESSES 100
PCB* all_processes[MAX_PROCESSES];
int  process_count      = 0;
int  next_pid           = 1;
int  selected_algorithm = 0;
int  total_memory       = 1024;
int  current_sim_id     = 0;  /* tracks which simulation a process belongs to */
MemoryBlock* memory_head = NULL;

/* ============================================================
 *  GTK WIDGET REFERENCES
 * ============================================================ */
GtkWidget* main_window;
GtkWidget* process_list_store;
GtkListStore* proc_store;
GtkListStore* mem_store;
GtkWidget* log_textview;
GtkWidget* status_bar;
GtkWidget* algo_label;
GtkWidget* proc_treeview;
GtkWidget* mem_treeview;
GtkWidget* notebook;

/* ============================================================
 *  FORWARD DECLARATIONS
 * ============================================================ */
void init_memory();
int  allocate_memory_first_fit(int pid, int size);
void free_memory(int pid);
void log_event(const char* event_type, const char* description, int pid);
void refresh_process_list();
void refresh_memory_list();
void refresh_log_view();
void set_status(const char* msg);
void run_fcfs();
void run_sjf();
void run_priority();
void show_simulation_results(const char* output);

/* Column enums for process list */
enum {
    COL_PID, COL_NAME, COL_PRIORITY, COL_ARRIVAL,
    COL_BURST, COL_MEMORY, COL_STATE, COL_WAIT, COL_TAT,
    NUM_PROC_COLS
};

/* Column enums for memory list */
enum {
    MCOL_START, MCOL_END, MCOL_SIZE, MCOL_STATUS, MCOL_PID,
    NUM_MEM_COLS
};

/* ============================================================
 *  MEMORY MANAGEMENT
 * ============================================================ */
void init_memory() {
    memory_head = (MemoryBlock*)malloc(sizeof(MemoryBlock));
    memory_head->start_address = 0;
    memory_head->size          = total_memory;
    memory_head->is_free       = 1;
    memory_head->process_id    = -1;
    memory_head->next          = NULL;
}

int allocate_memory_first_fit(int pid, int size) {
    MemoryBlock* cur = memory_head;
    while (cur) {
        if (cur->is_free && cur->size >= size) {
            if (cur->size == size) {
                cur->is_free    = 0;
                cur->process_id = pid;
                return cur->start_address;
            }
            MemoryBlock* nb = (MemoryBlock*)malloc(sizeof(MemoryBlock));
            nb->start_address = cur->start_address + size;
            nb->size          = cur->size - size;
            nb->is_free       = 1;
            nb->process_id    = -1;
            nb->next          = cur->next;
            cur->size         = size;
            cur->is_free      = 0;
            cur->process_id   = pid;
            cur->next         = nb;
            return cur->start_address;
        }
        cur = cur->next;
    }
    return -1;
}

void free_memory(int pid) {
    MemoryBlock* cur = memory_head;
    while (cur) {
        if (!cur->is_free && cur->process_id == pid) {
            cur->is_free    = 1;
            cur->process_id = -1;
            break;
        }
        cur = cur->next;
    }
    /* Coalesce adjacent free blocks */
    int merged;
    do {
        merged = 0;
        cur = memory_head;
        while (cur && cur->next) {
            if (cur->is_free && cur->next->is_free) {
                cur->size += cur->next->size;
                MemoryBlock* tmp = cur->next;
                cur->next = tmp->next;
                free(tmp);
                merged = 1;
            } else {
                cur = cur->next;
            }
        }
    } while (merged);
}

/* ============================================================
 *  FILE LOGGING
 * ============================================================ */
void log_event(const char* event_type, const char* description, int pid) {
    FILE* f = fopen("serc_log.txt", "a");
    if (f) {
        time_t now = time(NULL);
        struct tm* t = localtime(&now);
        fprintf(f, "[%02d:%02d:%02d] %-20s | PID: %-3d | %s\n",
                t->tm_hour, t->tm_min, t->tm_sec,
                event_type, pid, description);
        fclose(f);
    }
}

/* ============================================================
 *  UI HELPERS
 * ============================================================ */
void set_status(const char* msg) {
    gtk_label_set_text(GTK_LABEL(status_bar), msg);
}

const char* state_to_string(ProcessState s) {
    switch (s) {
        case NEW:        return "NEW";
        case READY:      return "READY";
        case RUNNING:    return "RUNNING";
        case WAITING:    return "WAITING";
        case TERMINATED: return "TERMINATED";
        default:         return "UNKNOWN";
    }
}

void refresh_process_list() {
    gtk_list_store_clear(proc_store);
    for (int i = 0; i < process_count; i++) {
        GtkTreeIter iter;
        gtk_list_store_append(proc_store, &iter);
        gtk_list_store_set(proc_store, &iter,
            COL_PID,      all_processes[i]->pid,
            COL_NAME,     all_processes[i]->name,
            COL_PRIORITY, all_processes[i]->priority,
            COL_ARRIVAL,  all_processes[i]->arrival_time,
            COL_BURST,    all_processes[i]->burst_time,
            COL_MEMORY,   all_processes[i]->memory_required,
            COL_STATE,    state_to_string(all_processes[i]->state),
            COL_WAIT,     all_processes[i]->waiting_time,
            COL_TAT,      all_processes[i]->turnaround_time,
            -1);
    }
}

void refresh_memory_list() {
    gtk_list_store_clear(mem_store);
    MemoryBlock* cur = memory_head;
    while (cur) {
        GtkTreeIter iter;
        gtk_list_store_append(mem_store, &iter);
        char pid_str[10];
        if (cur->is_free) strcpy(pid_str, "-");
        else sprintf(pid_str, "%d", cur->process_id);
        gtk_list_store_set(mem_store, &iter,
            MCOL_START,  cur->start_address,
            MCOL_END,    cur->start_address + cur->size - 1,
            MCOL_SIZE,   cur->size,
            MCOL_STATUS, cur->is_free ? "FREE" : "ALLOCATED",
            MCOL_PID,    pid_str,
            -1);
        cur = cur->next;
    }
}

void refresh_log_view() {
    FILE* f = fopen("serc_log.txt", "r");
    GtkTextBuffer* buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(log_textview));
    gtk_text_buffer_set_text(buf, "", -1);
    if (!f) {
        gtk_text_buffer_set_text(buf, "No logs yet. Create processes and run simulations.\n", -1);
        return;
    }
    char line[256];
    GtkTextIter end;
    while (fgets(line, sizeof(line), f)) {
        gtk_text_buffer_get_end_iter(buf, &end);
        gtk_text_buffer_insert(buf, &end, line, -1);
    }
    fclose(f);
    /* Scroll to bottom */
    gtk_text_buffer_get_end_iter(buf, &end);
    GtkTextMark* mark = gtk_text_buffer_create_mark(buf, NULL, &end, FALSE);
    gtk_text_view_scroll_to_mark(GTK_TEXT_VIEW(log_textview), mark, 0.0, FALSE, 0.0, 0.0);
}

void show_simulation_results(const char* output) {
    GtkWidget* dialog = gtk_dialog_new_with_buttons(
        "Simulation Results",
        GTK_WINDOW(main_window),
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        "Close", GTK_RESPONSE_CLOSE,
        NULL);

    gtk_window_set_default_size(GTK_WINDOW(dialog), 700, 500);

    GtkWidget* scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);

    GtkWidget* tv = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(tv), FALSE);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(tv), TRUE);
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(tv), 10);
    gtk_text_view_set_top_margin(GTK_TEXT_VIEW(tv), 10);

    GtkTextBuffer* buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(tv));
    gtk_text_buffer_set_text(buf, output, -1);

    gtk_container_add(GTK_CONTAINER(scroll), tv);
    GtkWidget* content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    gtk_box_pack_start(GTK_BOX(content), scroll, TRUE, TRUE, 0);

    gtk_widget_show_all(dialog);
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
}

/* Callback: warn user if they type a priority value outside 1-5 */
void on_priority_spin_changed(GtkSpinButton* spin, gpointer data) {
    const char* raw = gtk_entry_get_text(GTK_ENTRY(spin));
    int val = atoi(raw);
    if (val > 5) {
        GtkWidget* warn = gtk_message_dialog_new(GTK_WINDOW(main_window),
            GTK_DIALOG_MODAL, GTK_MESSAGE_WARNING, GTK_BUTTONS_OK,
            "Invalid Priority Value!\n\n"
            "You entered %d but priority must be between 1 and 5.\n"
            "(1 = highest emergency, 5 = lowest)\n\n"
            "The value has been reset to 5.",
            val);
        gtk_dialog_run(GTK_DIALOG(warn));
        gtk_widget_destroy(warn);
        gtk_spin_button_set_value(spin, 5);
    } else if (val < 1 && strlen(raw) > 0) {
        GtkWidget* warn = gtk_message_dialog_new(GTK_WINDOW(main_window),
            GTK_DIALOG_MODAL, GTK_MESSAGE_WARNING, GTK_BUTTONS_OK,
            "Invalid Priority Value!\n\n"
            "Priority must be at least 1 (highest emergency).\n\n"
            "The value has been reset to 1.");
        gtk_dialog_run(GTK_DIALOG(warn));
        gtk_widget_destroy(warn);
        gtk_spin_button_set_value(spin, 1);
    }
}

/* Callback: warn user if they type a memory value exceeding total system memory */
void on_mem_spin_changed(GtkSpinButton* spin, gpointer data) {
    const char* raw = gtk_entry_get_text(GTK_ENTRY(spin));
    int val = atoi(raw);
    if (val > total_memory) {
        GtkWidget* warn = gtk_message_dialog_new(GTK_WINDOW(main_window),
            GTK_DIALOG_MODAL, GTK_MESSAGE_WARNING, GTK_BUTTONS_OK,
            "Invalid Memory Value!\n\n"
            "You entered %d KB but the total system memory is only %d KB.\n\n"
            "The value has been reset to the maximum (%d KB).",
            val, total_memory, total_memory);
        gtk_dialog_run(GTK_DIALOG(warn));
        gtk_widget_destroy(warn);
        gtk_spin_button_set_value(spin, total_memory);
    }
}

/* ============================================================
 *  CREATE PROCESS DIALOG
 * ============================================================ */
void on_create_process(GtkButton* btn, gpointer data) {
    GtkWidget* dialog = gtk_dialog_new_with_buttons(
        "Create Emergency Process",
        GTK_WINDOW(main_window),
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        "Create", GTK_RESPONSE_OK,
        "Cancel", GTK_RESPONSE_CANCEL,
        NULL);

    gtk_window_set_default_size(GTK_WINDOW(dialog), 400, 420);

    GtkWidget* content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget* grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 10);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 10);
    gtk_container_set_border_width(GTK_CONTAINER(grid), 20);
    gtk_box_pack_start(GTK_BOX(content), grid, TRUE, TRUE, 0);

    /* Process Type */
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Process Type:"), 0, 0, 1, 1);
    GtkWidget* type_combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(type_combo), "Ambulance Dispatch");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(type_combo), "Fire Service Alert");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(type_combo), "Police Coordination");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(type_combo), "Custom");
    gtk_combo_box_set_active(GTK_COMBO_BOX(type_combo), 0);
    gtk_grid_attach(GTK_GRID(grid), type_combo, 1, 0, 1, 1);

    /* Custom Name */
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Custom Name:"), 0, 1, 1, 1);
    GtkWidget* name_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(name_entry), "Only if Custom selected");
    gtk_grid_attach(GTK_GRID(grid), name_entry, 1, 1, 1, 1);

    /* Priority */
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Priority (1=highest, 5=lowest):"), 0, 2, 1, 1);
    GtkWidget* priority_spin = gtk_spin_button_new_with_range(1, 5, 1);
    g_signal_connect(priority_spin, "value-changed", G_CALLBACK(on_priority_spin_changed), NULL);
    GtkWidget* pri_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_box_pack_start(GTK_BOX(pri_box), priority_spin, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(pri_box), gtk_label_new("(min: 1, max: 5)"), FALSE, FALSE, 0);
    gtk_grid_attach(GTK_GRID(grid), pri_box, 1, 2, 1, 1);

    /* Burst Time */
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Burst Time (ms):"), 0, 3, 1, 1);
    GtkWidget* burst_spin = gtk_spin_button_new_with_range(1, 1000, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(burst_spin), 50);
    gtk_widget_set_size_request(burst_spin, 100, -1);
    gtk_grid_attach(GTK_GRID(grid), burst_spin, 1, 3, 1, 1);

    /* Arrival Time */
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Arrival Time (ms):"), 0, 4, 1, 1);
    GtkWidget* arrival_spin = gtk_spin_button_new_with_range(0, 10000, 1);
    gtk_widget_set_size_request(arrival_spin, 100, -1);
    gtk_grid_attach(GTK_GRID(grid), arrival_spin, 1, 4, 1, 1);

    /* Memory Required */
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Memory Required (KB):"), 0, 5, 1, 1);
    GtkWidget* mem_spin = gtk_spin_button_new_with_range(1, total_memory, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(mem_spin), 64);
    g_signal_connect(mem_spin, "value-changed", G_CALLBACK(on_mem_spin_changed), NULL);
    GtkWidget* mem_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_box_pack_start(GTK_BOX(mem_box), mem_spin, FALSE, FALSE, 0);
    char mem_hint[40];
    sprintf(mem_hint, "(min: 1, max: %d KB)", total_memory);
    gtk_box_pack_start(GTK_BOX(mem_box), gtk_label_new(mem_hint), FALSE, FALSE, 0);
    gtk_grid_attach(GTK_GRID(grid), mem_box, 1, 5, 1, 1);

    gtk_widget_show_all(dialog);
    int response = gtk_dialog_run(GTK_DIALOG(dialog));

    if (response == GTK_RESPONSE_OK) {
        PCB* p = (PCB*)malloc(sizeof(PCB));
        p->pid = next_pid++;

        int type_idx = gtk_combo_box_get_active(GTK_COMBO_BOX(type_combo));
        switch (type_idx) {
            case 0: strcpy(p->name, "Ambulance"); break;
            case 1: strcpy(p->name, "Fire");      break;
            case 2: strcpy(p->name, "Police");    break;
            case 3: {
                const char* custom = gtk_entry_get_text(GTK_ENTRY(name_entry));
                if (strlen(custom) > 0) {
                    strncpy(p->name, custom, 29);
                    p->name[29] = '\0';
                } else
                    strcpy(p->name, "Custom");
                break;
            }
        }

        /* Read the memory value entered by the user */
        const char* raw_mem = gtk_entry_get_text(GTK_ENTRY(mem_spin));
        int raw_mem_value = atoi(raw_mem);

        p->priority        = (int)gtk_spin_button_get_value(GTK_SPIN_BUTTON(priority_spin));
        p->burst_time      = (int)gtk_spin_button_get_value(GTK_SPIN_BUTTON(burst_spin));
        p->arrival_time    = (int)gtk_spin_button_get_value(GTK_SPIN_BUTTON(arrival_spin));
        p->memory_required = raw_mem_value;
        p->remaining_time  = p->burst_time;
        p->waiting_time    = 0;
        p->turnaround_time = 0;
        p->state           = NEW;
        p->sim_id          = 0;  /* not yet assigned to a simulation */
        p->next            = NULL;

        /* Validate memory request before even attempting allocation */
        if (raw_mem_value <= 0 || raw_mem_value > total_memory) {
            GtkWidget* warn = gtk_message_dialog_new(GTK_WINDOW(main_window),
                GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,
                "Invalid Memory Request!\n\n"
                "Process '%s' requested %d KB but the total system memory is only %d KB.\n\n"
                "Please create the process again with a valid memory size (1 - %d KB).",
                p->name, raw_mem_value, total_memory, total_memory);
            gtk_dialog_run(GTK_DIALOG(warn));
            gtk_widget_destroy(warn);
            free(p);
            gtk_widget_destroy(dialog);
            return;
        }

        p->memory_address = allocate_memory_first_fit(p->pid, p->memory_required);

        /* Check available free memory for better error reporting */
        int free_available = 0;
        MemoryBlock* cur = memory_head;
        while (cur) {
            if (cur->is_free && cur->size > free_available)
                free_available = cur->size; /* largest free block */
            cur = cur->next;
        }

        char log_desc[150];
        if (p->memory_address == -1) {
            /* Process enters READY state upon creation (NEW -> READY).
               It will transition to WAITING during scheduling if memory
               is unavailable at execution time. */
            p->state = READY;
            sprintf(log_desc, "Process '%s' created with no memory - needs %dKB but only %dKB free",
                    p->name, p->memory_required, free_available);
            log_event("MEMORY_FAIL", log_desc, p->pid);
            set_status("WARNING: Not enough memory! Process will enter WAITING during scheduling.");

            GtkWidget* warn = gtk_message_dialog_new(GTK_WINDOW(main_window),
                GTK_DIALOG_MODAL, GTK_MESSAGE_WARNING, GTK_BUTTONS_OK,
                "Memory Allocation Failed!\n\n"
                "Process '%s' needs %d KB but only %d KB is available.\n\n"
                "Process is placed in READY state.\n"
                "When the scheduler tries to run it, it will transition\n"
                "to WAITING until memory becomes available.",
                p->name, p->memory_required, free_available);
            gtk_dialog_run(GTK_DIALOG(warn));
            gtk_widget_destroy(warn);
        } else {
            /* Memory allocated - process is READY */
            p->state = READY;
            sprintf(log_desc, "Process '%s' created - Priority:%d Burst:%dms Memory:%dKB@%dKB",
                    p->name, p->priority, p->burst_time,
                    p->memory_required, p->memory_address);
            log_event("PROCESS_CREATED", log_desc, p->pid);
            char status[100];
            sprintf(status, "Process %d (%s) created successfully!", p->pid, p->name);
            set_status(status);
        }

        if (process_count >= MAX_PROCESSES) {
            g_print("Maximum process limit reached.\n");
            free(p);
            gtk_widget_destroy(dialog);
            return;
        }
        all_processes[process_count++] = p;
        refresh_process_list();
        refresh_memory_list();
        refresh_log_view();

        /* Switch to process tab */
        gtk_notebook_set_current_page(GTK_NOTEBOOK(notebook), 0);
    }

    gtk_widget_destroy(dialog);
}

/* ============================================================
 *  ALGORITHM SELECTION DIALOG
 * ============================================================ */
void on_select_algorithm(GtkButton* btn, gpointer data) {
    GtkWidget* dialog = gtk_dialog_new_with_buttons(
        "Select Scheduling Algorithm",
        GTK_WINDOW(main_window),
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        "Select", GTK_RESPONSE_OK,
        "Cancel", GTK_RESPONSE_CANCEL,
        NULL);

    GtkWidget* content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget* vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 20);
    gtk_box_pack_start(GTK_BOX(content), vbox, TRUE, TRUE, 0);

    gtk_box_pack_start(GTK_BOX(vbox),
        gtk_label_new("Choose a CPU scheduling algorithm:"), FALSE, FALSE, 0);

    GtkWidget* r1 = gtk_radio_button_new_with_label(NULL, "First Come First Serve (FCFS)");
    GtkWidget* r2 = gtk_radio_button_new_with_label_from_widget(GTK_RADIO_BUTTON(r1), "Shortest Job First (SJF)");
    GtkWidget* r3 = gtk_radio_button_new_with_label_from_widget(GTK_RADIO_BUTTON(r1), "Priority Scheduling (Emergency Level)");

    /* Set active based on current selection */
    if      (selected_algorithm == 1) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(r1), TRUE);
    else if (selected_algorithm == 2) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(r2), TRUE);
    else if (selected_algorithm == 3) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(r3), TRUE);

    gtk_box_pack_start(GTK_BOX(vbox), r1, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), r2, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), r3, FALSE, FALSE, 0);

    gtk_widget_show_all(dialog);
    int response = gtk_dialog_run(GTK_DIALOG(dialog));

    if (response == GTK_RESPONSE_OK) {
        if      (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(r1))) selected_algorithm = 1;
        else if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(r2))) selected_algorithm = 2;
        else if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(r3))) selected_algorithm = 3;

        const char* names[] = {"", "FCFS", "SJF", "Priority Scheduling"};
        char algo_text[60];
        sprintf(algo_text, "Algorithm: %s", names[selected_algorithm]);
        gtk_label_set_text(GTK_LABEL(algo_label), algo_text);
        set_status("Scheduling algorithm updated.");
        log_event("ALGO_SELECT", names[selected_algorithm], -1);
    }

    gtk_widget_destroy(dialog);
}

/* ============================================================
 *  SIMULATION
 * ============================================================ */
void on_run_simulation(GtkButton* btn, gpointer data) {
    if (process_count == 0) {
        GtkWidget* d = gtk_message_dialog_new(GTK_WINDOW(main_window),
            GTK_DIALOG_MODAL, GTK_MESSAGE_WARNING, GTK_BUTTONS_OK,
            "No processes created yet!\nPlease create processes first.");
        gtk_dialog_run(GTK_DIALOG(d));
        gtk_widget_destroy(d);
        return;
    }
    if (selected_algorithm == 0) {
        GtkWidget* d = gtk_message_dialog_new(GTK_WINDOW(main_window),
            GTK_DIALOG_MODAL, GTK_MESSAGE_WARNING, GTK_BUTTONS_OK,
            "No scheduling algorithm selected!\nPlease select an algorithm first.");
        gtk_dialog_run(GTK_DIALOG(d));
        gtk_widget_destroy(d);
        return;
    }

    /* Check if there are terminated processes mixed with new ones */
    int has_terminated = 0, has_ready = 0;
    for (int i = 0; i < process_count; i++) {
        if (all_processes[i]->state == TERMINATED) has_terminated = 1;
        if (all_processes[i]->state == READY || all_processes[i]->state == WAITING) has_ready = 1;
    }
    if (has_terminated && has_ready) {
        GtkWidget* d = gtk_message_dialog_new(GTK_WINDOW(main_window),
            GTK_DIALOG_MODAL, GTK_MESSAGE_WARNING, GTK_BUTTONS_YES_NO,
            "You have a mix of new and already-terminated processes.\n\n"
            "Only the new (READY/WAITING) processes will be simulated.\n"
            "Terminated processes will be excluded.\n\n"
            "Do you want to continue?");
        int r = gtk_dialog_run(GTK_DIALOG(d));
        gtk_widget_destroy(d);
        if (r != GTK_RESPONSE_YES) return;
    }
    if (has_terminated && !has_ready) {
        GtkWidget* d = gtk_message_dialog_new(GTK_WINDOW(main_window),
            GTK_DIALOG_MODAL, GTK_MESSAGE_WARNING, GTK_BUTTONS_OK,
            "All processes are already terminated!\n\n"
            "Please clear all processes and create new ones,\n"
            "or create new processes before running again.");
        gtk_dialog_run(GTK_DIALOG(d));
        gtk_widget_destroy(d);
        return;
    }

    /* Increment simulation ID so we can track which processes belong to this run */
    current_sim_id++;

    /* Step 1: Reset memory to one big free block */
    MemoryBlock* cur = memory_head;
    while (cur) {
        MemoryBlock* tmp = cur->next;
        free(cur);
        cur = tmp;
    }
    init_memory();

    /* Step 2: Reset only non-terminated processes and assign current sim_id */
    for (int i = 0; i < process_count; i++) {
        if (all_processes[i]->state == TERMINATED) continue;
        all_processes[i]->remaining_time  = all_processes[i]->burst_time;
        all_processes[i]->waiting_time    = 0;
        all_processes[i]->turnaround_time = 0;
        all_processes[i]->memory_address  = -1;
        all_processes[i]->state           = READY;
        all_processes[i]->sim_id          = current_sim_id;
    }

    /* Step 3: Try to allocate memory for each non-terminated process */
    for (int i = 0; i < process_count; i++) {
        if (all_processes[i]->sim_id != current_sim_id) continue;
        int addr = allocate_memory_first_fit(all_processes[i]->pid,
                                             all_processes[i]->memory_required);
        all_processes[i]->memory_address = addr;
        if (addr == -1)
            all_processes[i]->state = WAITING;
    }

    refresh_memory_list();

    const char* algo_names[] = {"", "FCFS", "SJF", "Priority Scheduling"};
    char sim_log[100];
    sprintf(sim_log, "Simulation started using %s", algo_names[selected_algorithm]);
    log_event("SIMULATION_START", sim_log, -1);

    /* Build output string */
    char output[32768];  /* large buffer to handle many processes */
    memset(output, 0, sizeof(output));
    char line[256];

    sprintf(line, "SERC MINI-OS - SIMULATION RESULTS\n");
    strcat(output, line);
    sprintf(line, "Algorithm: %s\n", algo_names[selected_algorithm]);
    strcat(output, line);
    strcat(output, "================================================\n\n");

    int current_time = 0;
    float total_wait = 0, total_tat = 0;

    /* ---- FCFS ---- */
    if (selected_algorithm == 1) {
        /* Sort by arrival time */
        for (int i = 0; i < process_count - 1; i++)
            for (int j = 0; j < process_count - i - 1; j++)
                if (all_processes[j]->arrival_time > all_processes[j+1]->arrival_time) {
                    PCB* tmp = all_processes[j];
                    all_processes[j] = all_processes[j+1];
                    all_processes[j+1] = tmp;
                }

        strcat(output, "Execution Order:\n");
        int completed = 0;
        int pass = 0;
        int total_in_sim = 0;
        for (int i = 0; i < process_count; i++)
            if (all_processes[i]->sim_id == current_sim_id) total_in_sim++;
        int max_passes = total_in_sim * total_in_sim + total_in_sim;

        while (completed < total_in_sim && pass < max_passes) {
            pass++;

            /* First: try to wake any WAITING processes if memory is available */
            for (int w = 0; w < process_count; w++) {
                if (all_processes[w]->sim_id != current_sim_id) continue;
                if (all_processes[w]->state == WAITING) {
                    int addr = allocate_memory_first_fit(all_processes[w]->pid,
                                                         all_processes[w]->memory_required);
                    if (addr != -1) {
                        all_processes[w]->memory_address = addr;
                        all_processes[w]->state          = READY;
                        sprintf(line, "  Time %3d: [PID %d] %s WAITING->READY (memory available)\n",
                                current_time, all_processes[w]->pid, all_processes[w]->name);
                        strcat(output, line);
                        log_event("STATE_CHANGE", "WAITING->READY", all_processes[w]->pid);
                    }
                }
            }

            /* Then: find next READY process in arrival order */
            int found = 0;
            for (int i = 0; i < process_count; i++) {
                if (all_processes[i]->sim_id != current_sim_id) continue;
                if (all_processes[i]->state != READY) continue;

                if (current_time < all_processes[i]->arrival_time) {
                    sprintf(line, "  Time %d: CPU idle until %d ms\n",
                            current_time, all_processes[i]->arrival_time);
                    strcat(output, line);
                    current_time = all_processes[i]->arrival_time;
                }

                all_processes[i]->state        = RUNNING;
                all_processes[i]->waiting_time = current_time - all_processes[i]->arrival_time;
                if (all_processes[i]->waiting_time < 0) all_processes[i]->waiting_time = 0;
                sprintf(line, "  Time %3d: [PID %d] %s READY->RUNNING (burst: %d ms)\n",
                        current_time, all_processes[i]->pid,
                        all_processes[i]->name, all_processes[i]->burst_time);
                strcat(output, line);
                current_time += all_processes[i]->burst_time;
                all_processes[i]->turnaround_time = current_time - all_processes[i]->arrival_time;
                all_processes[i]->state = TERMINATED;
                free_memory(all_processes[i]->pid);
                sprintf(line, "  Time %3d: [PID %d] %s RUNNING->TERMINATED\n\n",
                        current_time, all_processes[i]->pid, all_processes[i]->name);
                strcat(output, line);
                log_event("STATE_CHANGE", "RUNNING->TERMINATED", all_processes[i]->pid);
                completed++;
                found = 1;
                break;
            }

            if (!found) {
                int any_waiting = 0;
                for (int i = 0; i < process_count; i++)
                    if (all_processes[i]->sim_id == current_sim_id &&
                        all_processes[i]->state == WAITING) { any_waiting = 1; break; }
                if (!any_waiting) break;
            }
        }
    }


    /* ---- SJF ---- */
    else if (selected_algorithm == 2) {
        strcat(output, "Execution Order (Shortest Job First):\n");
        int completed = 0;
        int total_in_sim = 0;
        for (int i = 0; i < process_count; i++)
            if (all_processes[i]->sim_id == current_sim_id) total_in_sim++;

        while (completed < total_in_sim) {
            int chosen = -1;
            for (int i = 0; i < process_count; i++) {
                if (all_processes[i]->sim_id != current_sim_id) continue;
                if (all_processes[i]->state != READY) continue;
                if (all_processes[i]->arrival_time > current_time) continue;
                if (chosen == -1 || all_processes[i]->burst_time < all_processes[chosen]->burst_time)
                    chosen = i;
            }
            if (chosen == -1) {
                int next_arrival = -1;
                for (int i = 0; i < process_count; i++) {
                    if (all_processes[i]->sim_id != current_sim_id) continue;
                    if (all_processes[i]->state == READY) {
                        if (next_arrival == -1 || all_processes[i]->arrival_time < next_arrival)
                            next_arrival = all_processes[i]->arrival_time;
                    }
                }
                if (next_arrival == -1) break;
                sprintf(line, "  Time %d: CPU idle until %d ms\n", current_time, next_arrival);
                strcat(output, line);
                current_time = next_arrival;
                continue;
            }
            if (all_processes[chosen]->memory_address == -1) {
                int addr = allocate_memory_first_fit(all_processes[chosen]->pid,
                                                     all_processes[chosen]->memory_required);
                if (addr == -1) {
                    all_processes[chosen]->state = RUNNING;
                    sprintf(line, "  Time %3d: [PID %d] %s READY->RUNNING->WAITING (no memory)\n",
                            current_time, all_processes[chosen]->pid, all_processes[chosen]->name);
                    strcat(output, line);
                    all_processes[chosen]->state = WAITING;
                    int any_ready = 0;
                    for (int i = 0; i < process_count; i++)
                        if (all_processes[i]->sim_id == current_sim_id &&
                            all_processes[i]->state == READY) { any_ready = 1; break; }
                    if (!any_ready) break;
                    continue;
                }
                all_processes[chosen]->memory_address = addr;
            }
            all_processes[chosen]->state        = RUNNING;
            all_processes[chosen]->waiting_time = current_time - all_processes[chosen]->arrival_time;
            if (all_processes[chosen]->waiting_time < 0) all_processes[chosen]->waiting_time = 0;
            sprintf(line, "  Time %3d: [PID %d] %s READY->RUNNING (burst: %d ms)\n",
                    current_time, all_processes[chosen]->pid,
                    all_processes[chosen]->name, all_processes[chosen]->burst_time);
            strcat(output, line);
            current_time += all_processes[chosen]->burst_time;
            all_processes[chosen]->turnaround_time = current_time - all_processes[chosen]->arrival_time;
            all_processes[chosen]->state = TERMINATED;
            free_memory(all_processes[chosen]->pid);
            sprintf(line, "  Time %3d: [PID %d] %s RUNNING->TERMINATED\n\n",
                    current_time, all_processes[chosen]->pid, all_processes[chosen]->name);
            strcat(output, line);
            completed++;
            for (int w = 0; w < process_count; w++) {
                if (all_processes[w]->sim_id != current_sim_id) continue;
                if (all_processes[w]->state == WAITING) {
                    int addr = allocate_memory_first_fit(all_processes[w]->pid,
                                                         all_processes[w]->memory_required);
                    if (addr != -1) {
                        all_processes[w]->memory_address = addr;
                        all_processes[w]->state          = READY;
                        sprintf(line, "  Time %3d: [PID %d] %s WAITING->READY (memory available)\n",
                                current_time, all_processes[w]->pid, all_processes[w]->name);
                        strcat(output, line);
                    }
                }
            }
        }
    }

    /* ---- Priority Scheduling ---- */
    else if (selected_algorithm == 3) {
        strcat(output, "Execution Order (By Emergency Priority):\n");
        strcat(output, "Note: Priority 1 = highest emergency\n\n");
        int completed = 0;
        int total_in_sim = 0;
        for (int i = 0; i < process_count; i++)
            if (all_processes[i]->sim_id == current_sim_id) total_in_sim++;

        while (completed < total_in_sim) {
            int chosen = -1;
            for (int i = 0; i < process_count; i++) {
                if (all_processes[i]->sim_id != current_sim_id) continue;
                if (all_processes[i]->state != READY) continue;
                if (all_processes[i]->arrival_time > current_time) continue;
                if (chosen == -1 ||
                    all_processes[i]->priority < all_processes[chosen]->priority ||
                   (all_processes[i]->priority == all_processes[chosen]->priority &&
                    all_processes[i]->pid < all_processes[chosen]->pid))
                    chosen = i;
            }
            if (chosen == -1) {
                int next_arrival = -1;
                for (int i = 0; i < process_count; i++) {
                    if (all_processes[i]->sim_id != current_sim_id) continue;
                    if (all_processes[i]->state == READY) {
                        if (next_arrival == -1 || all_processes[i]->arrival_time < next_arrival)
                            next_arrival = all_processes[i]->arrival_time;
                    }
                }
                if (next_arrival == -1) break;
                sprintf(line, "  Time %d: CPU idle until %d ms\n", current_time, next_arrival);
                strcat(output, line);
                current_time = next_arrival;
                continue;
            }
            if (all_processes[chosen]->memory_address == -1) {
                int addr = allocate_memory_first_fit(all_processes[chosen]->pid,
                                                     all_processes[chosen]->memory_required);
                if (addr == -1) {
                    all_processes[chosen]->state = RUNNING;
                    sprintf(line, "  Time %3d: [PID %d] %s READY->RUNNING->WAITING (no memory)\n",
                            current_time, all_processes[chosen]->pid, all_processes[chosen]->name);
                    strcat(output, line);
                    all_processes[chosen]->state = WAITING;
                    int any_ready = 0;
                    for (int i = 0; i < process_count; i++)
                        if (all_processes[i]->sim_id == current_sim_id &&
                            all_processes[i]->state == READY) { any_ready = 1; break; }
                    if (!any_ready) break;
                    continue;
                }
                all_processes[chosen]->memory_address = addr;
            }
            all_processes[chosen]->state        = RUNNING;
            all_processes[chosen]->waiting_time = current_time - all_processes[chosen]->arrival_time;
            if (all_processes[chosen]->waiting_time < 0) all_processes[chosen]->waiting_time = 0;
            sprintf(line, "  Time %3d: [PID %d] %s [Priority %d] READY->RUNNING (burst: %d ms)\n",
                    current_time, all_processes[chosen]->pid, all_processes[chosen]->name,
                    all_processes[chosen]->priority, all_processes[chosen]->burst_time);
            strcat(output, line);
            current_time += all_processes[chosen]->burst_time;
            all_processes[chosen]->turnaround_time = current_time - all_processes[chosen]->arrival_time;
            all_processes[chosen]->state = TERMINATED;
            free_memory(all_processes[chosen]->pid);
            sprintf(line, "  Time %3d: [PID %d] %s RUNNING->TERMINATED\n\n",
                    current_time, all_processes[chosen]->pid, all_processes[chosen]->name);
            strcat(output, line);
            completed++;
            for (int w = 0; w < process_count; w++) {
                if (all_processes[w]->sim_id != current_sim_id) continue;
                if (all_processes[w]->state == WAITING) {
                    int addr = allocate_memory_first_fit(all_processes[w]->pid,
                                                         all_processes[w]->memory_required);
                    if (addr != -1) {
                        all_processes[w]->memory_address = addr;
                        all_processes[w]->state          = READY;
                        sprintf(line, "  Time %3d: [PID %d] %s WAITING->READY (memory available)\n",
                                current_time, all_processes[w]->pid, all_processes[w]->name);
                        strcat(output, line);
                    }
                }
            }
        }
    }

    /* Metrics table - only show processes from current simulation */
    strcat(output, "\n================================================\n");
    strcat(output, "PER PROCESS METRICS:\n");
    strcat(output, "================================================\n");
    sprintf(line, "%-4s %-13s %-8s %-10s %-12s\n",
            "PID", "Name", "Burst", "Wait(ms)", "Turnaround");
    strcat(output, line);
    strcat(output, "------------------------------------------------\n");

    int ran_count = 0;
    for (int i = 0; i < process_count; i++) {
        if (all_processes[i]->sim_id != current_sim_id) continue;
        if (all_processes[i]->state == WAITING) {
            sprintf(line, "%-4d %-13s %-8d %-10s %-12s  [WAITING - no memory]\n",
                    all_processes[i]->pid, all_processes[i]->name,
                    all_processes[i]->burst_time, "-", "-");
            strcat(output, line);
        } else {
            total_wait += all_processes[i]->waiting_time;
            total_tat  += all_processes[i]->turnaround_time;
            ran_count++;
            sprintf(line, "%-4d %-13s %-8d %-10d %-12d\n",
                    all_processes[i]->pid, all_processes[i]->name,
                    all_processes[i]->burst_time,
                    all_processes[i]->waiting_time,
                    all_processes[i]->turnaround_time);
            strcat(output, line);
        }
    }

    strcat(output, "================================================\n");
    sprintf(line, "Total Time          : %d ms\n", current_time);
    strcat(output, line);
    if (ran_count > 0) {
        sprintf(line, "Avg Waiting Time    : %.2f ms\n", total_wait / (float)ran_count);
        strcat(output, line);
        sprintf(line, "Avg Turnaround Time : %.2f ms\n", total_tat  / (float)ran_count);
        strcat(output, line);
    }

    /* Count total in current simulation for processes run display */
    int total_in_sim_count = 0;
    for (int i = 0; i < process_count; i++)
        if (all_processes[i]->sim_id == current_sim_id) total_in_sim_count++;
    sprintf(line, "Processes Run       : %d / %d\n", ran_count, total_in_sim_count);
    strcat(output, line);

    /* Calculate CPU utilization: only count burst times from current simulation */
    int total_burst = 0;
    for (int i = 0; i < process_count; i++) {
        if (all_processes[i]->sim_id == current_sim_id &&
            all_processes[i]->state == TERMINATED)
            total_burst += all_processes[i]->burst_time;
    }
    float cpu_util = current_time > 0 ? (total_burst / (float)current_time) * 100.0f : 0.0f;
    sprintf(line, "CPU Utilization     : %.2f%%\n", cpu_util);
    strcat(output, line);

    if (ran_count < total_in_sim_count)
        strcat(output, "\nNOTE: Some processes are in WAITING state due to insufficient memory.\n");

    /* Log end */
    char summary[150];
    sprintf(summary, "%s done - AvgWait:%.2f ms AvgTAT:%.2f ms Ran:%d/%d",
            algo_names[selected_algorithm],
            ran_count > 0 ? total_wait / (float)ran_count : 0,
            ran_count > 0 ? total_tat  / (float)ran_count : 0,
            ran_count, process_count);
    log_event("SIMULATION_END", summary, -1);

    refresh_process_list();
    refresh_memory_list();
    refresh_log_view();
    /* Switch to process tab to show updated process states */
    gtk_notebook_set_current_page(GTK_NOTEBOOK(notebook), 0);
    set_status("Simulation complete! See results window.");

    show_simulation_results(output);

    /* Refresh all views after simulation results window closes */
    refresh_memory_list();
    refresh_process_list();
}

/* ============================================================
 *  CLEAR ALL
 * ============================================================ */
void on_clear_all(GtkButton* btn, gpointer data) {
    GtkWidget* d = gtk_message_dialog_new(GTK_WINDOW(main_window),
        GTK_DIALOG_MODAL, GTK_MESSAGE_QUESTION,
        GTK_BUTTONS_YES_NO,
        "Clear all processes and reset memory?");
    int r = gtk_dialog_run(GTK_DIALOG(d));
    gtk_widget_destroy(d);
    if (r != GTK_RESPONSE_YES) return;

    for (int i = 0; i < process_count; i++) free(all_processes[i]);
    process_count = 0;
    next_pid      = 1;

    /* Reset memory */
    MemoryBlock* cur = memory_head;
    while (cur) {
        MemoryBlock* tmp = cur->next;
        free(cur);
        cur = tmp;
    }
    init_memory();

    refresh_process_list();
    refresh_memory_list();
    log_event("SYSTEM", "All processes cleared and memory reset", -1);
    refresh_log_view();
    set_status("All processes cleared. Memory reset.");
}

/* ============================================================
 *  BUILD UI
 * ============================================================ */
GtkWidget* build_process_tab() {
    GtkWidget* vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 10);

    /* Create list store */
    proc_store = gtk_list_store_new(NUM_PROC_COLS,
        G_TYPE_INT, G_TYPE_STRING, G_TYPE_INT, G_TYPE_INT,
        G_TYPE_INT, G_TYPE_INT, G_TYPE_STRING, G_TYPE_INT, G_TYPE_INT);

    proc_treeview = gtk_tree_view_new_with_model(GTK_TREE_MODEL(proc_store));
    gtk_tree_view_set_grid_lines(GTK_TREE_VIEW(proc_treeview), GTK_TREE_VIEW_GRID_LINES_BOTH);

    const char* col_titles[] = {
        "PID", "Name", "Priority", "Arrival(ms)",
        "Burst(ms)", "Memory(KB)", "State", "Wait(ms)", "Turnaround(ms)"
    };
    for (int i = 0; i < NUM_PROC_COLS; i++) {
        GtkCellRenderer* r = gtk_cell_renderer_text_new();
        GtkTreeViewColumn* col = gtk_tree_view_column_new_with_attributes(
            col_titles[i], r, "text", i, NULL);
        gtk_tree_view_column_set_resizable(col, TRUE);
        gtk_tree_view_column_set_min_width(col, 80);
        gtk_tree_view_append_column(GTK_TREE_VIEW(proc_treeview), col);
    }

    GtkWidget* scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(scroll), proc_treeview);
    gtk_box_pack_start(GTK_BOX(vbox), scroll, TRUE, TRUE, 0);

    return vbox;
}

GtkWidget* build_memory_tab() {
    GtkWidget* vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 10);

    mem_store = gtk_list_store_new(NUM_MEM_COLS,
        G_TYPE_INT, G_TYPE_INT, G_TYPE_INT, G_TYPE_STRING, G_TYPE_STRING);

    mem_treeview = gtk_tree_view_new_with_model(GTK_TREE_MODEL(mem_store));
    gtk_tree_view_set_grid_lines(GTK_TREE_VIEW(mem_treeview), GTK_TREE_VIEW_GRID_LINES_BOTH);

    const char* col_titles[] = {"Start(KB)", "End(KB)", "Size(KB)", "Status", "Process ID"};
    for (int i = 0; i < NUM_MEM_COLS; i++) {
        GtkCellRenderer* r = gtk_cell_renderer_text_new();
        GtkTreeViewColumn* col = gtk_tree_view_column_new_with_attributes(
            col_titles[i], r, "text", i, NULL);
        gtk_tree_view_column_set_resizable(col, TRUE);
        gtk_tree_view_column_set_min_width(col, 100);
        gtk_tree_view_append_column(GTK_TREE_VIEW(mem_treeview), col);
    }

    GtkWidget* scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(scroll), mem_treeview);
    gtk_box_pack_start(GTK_BOX(vbox), scroll, TRUE, TRUE, 0);

    return vbox;
}

/* Clear logs callback */
void on_clear_logs(GtkButton* btn, gpointer data) {
    GtkWidget* d = gtk_message_dialog_new(GTK_WINDOW(main_window),
        GTK_DIALOG_MODAL, GTK_MESSAGE_QUESTION, GTK_BUTTONS_YES_NO,
        "Clear all system logs?");
    int r = gtk_dialog_run(GTK_DIALOG(d));
    gtk_widget_destroy(d);
    if (r != GTK_RESPONSE_YES) return;

    /* Delete log file */
    remove("serc_log.txt");

    /* Clear text view */
    GtkTextBuffer* buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(log_textview));
    gtk_text_buffer_set_text(buf, "Logs cleared.\n", -1);

    log_event("SYSTEM", "Log file cleared by user", -1);
    refresh_log_view();
    set_status("System logs cleared.");
}

GtkWidget* build_log_tab() {
    GtkWidget* vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 10);

    log_textview = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(log_textview), FALSE);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(log_textview), TRUE);
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(log_textview), 8);
    gtk_text_view_set_top_margin(GTK_TEXT_VIEW(log_textview), 8);

    GtkWidget* scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(scroll), log_textview);
    gtk_box_pack_start(GTK_BOX(vbox), scroll, TRUE, TRUE, 0);

    /* Button row */
    GtkWidget* btn_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_pack_start(GTK_BOX(vbox), btn_row, FALSE, FALSE, 0);

    GtkWidget* refresh_btn = gtk_button_new_with_label("Refresh Logs");
    g_signal_connect(refresh_btn, "clicked", G_CALLBACK(refresh_log_view), NULL);
    gtk_box_pack_start(GTK_BOX(btn_row), refresh_btn, FALSE, FALSE, 0);

    /* Clear Logs button */
    GtkWidget* clear_btn = gtk_button_new_with_label("Clear Logs");
    g_signal_connect(clear_btn, "clicked", G_CALLBACK(on_clear_logs), NULL);
    gtk_box_pack_start(GTK_BOX(btn_row), clear_btn, FALSE, FALSE, 0);

    return vbox;
}

/* ============================================================
 *  MAIN
 * ============================================================ */
int main(int argc, char* argv[]) {
    gtk_init(&argc, &argv);
    init_memory();
    log_event("SYSTEM", "SERC Mini-OS GUI Started", -1);

    /* Main window */
    main_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(main_window),
        "SERC Mini-OS | CS 225 | Smart Emergency Response Center");
    gtk_window_set_default_size(GTK_WINDOW(main_window), 1000, 650);
    gtk_window_set_position(GTK_WINDOW(main_window), GTK_WIN_POS_CENTER);
    g_signal_connect(main_window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    /* Outer vertical box */
    GtkWidget* outer_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(main_window), outer_vbox);

    /* ---- HEADER ---- */
    GtkWidget* header = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_set_name(header, "header");
    gtk_container_set_border_width(GTK_CONTAINER(header), 12);

    GtkWidget* title_label = gtk_label_new("SMART EMERGENCY RESPONSE CENTER");
    gtk_widget_set_name(title_label, "title_label");

    GtkWidget* sub_label = gtk_label_new("Mini Operating System  |  CS 225  |  Copperbelt University");
    gtk_widget_set_name(sub_label, "sub_label");

    gtk_box_pack_start(GTK_BOX(header), title_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(header), sub_label,   FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(outer_vbox), header,  FALSE, FALSE, 0);

    /* ---- TOOLBAR ---- */
    GtkWidget* toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_container_set_border_width(GTK_CONTAINER(toolbar), 8);
    gtk_widget_set_name(toolbar, "toolbar");

    GtkWidget* btn_create = gtk_button_new_with_label("+ New Process");
    GtkWidget* btn_algo   = gtk_button_new_with_label("Select Algorithm");
    GtkWidget* btn_run    = gtk_button_new_with_label("Run Simulation");
    GtkWidget* btn_clear  = gtk_button_new_with_label("Clear All");

    gtk_widget_set_name(btn_create, "btn_create");
    gtk_widget_set_name(btn_run,    "btn_run");
    gtk_widget_set_name(btn_clear,  "btn_clear");

    algo_label = gtk_label_new("Algorithm: None selected");
    gtk_widget_set_name(algo_label, "algo_label");

    gtk_box_pack_start(GTK_BOX(toolbar), btn_create, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(toolbar), btn_algo,   FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(toolbar), btn_run,    FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(toolbar), btn_clear,  FALSE, FALSE, 0);
    gtk_box_pack_end  (GTK_BOX(toolbar), algo_label, FALSE, FALSE, 10);

    gtk_box_pack_start(GTK_BOX(outer_vbox), toolbar, FALSE, FALSE, 0);

    /* Separator */
    gtk_box_pack_start(GTK_BOX(outer_vbox),
        gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), FALSE, FALSE, 0);

    /* ---- NOTEBOOK (tabs) ---- */
    notebook = gtk_notebook_new();
    gtk_notebook_set_tab_pos(GTK_NOTEBOOK(notebook), GTK_POS_TOP);
    gtk_box_pack_start(GTK_BOX(outer_vbox), notebook, TRUE, TRUE, 0);

    gtk_notebook_append_page(GTK_NOTEBOOK(notebook),
        build_process_tab(), gtk_label_new("  Processes  "));
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook),
        build_memory_tab(), gtk_label_new("  Memory  "));
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook),
        build_log_tab(), gtk_label_new("  System Logs  "));

    /* ---- STATUS BAR ---- */
    gtk_box_pack_start(GTK_BOX(outer_vbox),
        gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), FALSE, FALSE, 0);
    GtkWidget* status_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_container_set_border_width(GTK_CONTAINER(status_box), 4);
    status_bar = gtk_label_new("Ready. Create processes to begin.");
    gtk_label_set_xalign(GTK_LABEL(status_bar), 0.0f);
    gtk_box_pack_start(GTK_BOX(status_box), status_bar, TRUE, TRUE, 6);
    gtk_box_pack_start(GTK_BOX(outer_vbox), status_box, FALSE, FALSE, 0);

    /* ---- CONNECT SIGNALS ---- */
    g_signal_connect(btn_create, "clicked", G_CALLBACK(on_create_process),    NULL);
    g_signal_connect(btn_algo,   "clicked", G_CALLBACK(on_select_algorithm),  NULL);
    g_signal_connect(btn_run,    "clicked", G_CALLBACK(on_run_simulation),     NULL);
    g_signal_connect(btn_clear,  "clicked", G_CALLBACK(on_clear_all),          NULL);

    /* ---- CSS STYLING ---- */
    GtkCssProvider* css = gtk_css_provider_new();
    gtk_css_provider_load_from_data(css,
        "window { background-color: #1a1a2e; }"

        "#header { background-color: #16213e; border-bottom: 2px solid #e94560; }"
        "#title_label { color: #e94560; font-size: 20px; font-weight: bold; }"
        "#sub_label { color: #a0a0b0; font-size: 11px; }"

        "#toolbar { background-color: #16213e; }"
        "#algo_label { color: #00d4aa; font-style: italic; }"

        "button { background: #0f3460; color: #ffffff; border: 1px solid #e94560;"
        "         border-radius: 4px; padding: 6px 14px; font-weight: bold; }"
        "button:hover { background: #e94560; }"

        "#btn_run { background: #e94560; color: white; }"
        "#btn_run:hover { background: #c73652; }"
        "#btn_clear { background: #333; color: #aaa; border-color: #555; }"
        "#btn_clear:hover { background: #555; color: white; }"

        "notebook { background-color: #1a1a2e; }"
        "notebook tab { background: #16213e; color: #a0a0b0; padding: 6px 16px; }"
        "notebook tab:checked { background: #0f3460; color: #e94560; font-weight: bold; }"

        "treeview { background-color: #16213e; color: #e0e0e0; font-family: monospace; }"
        "treeview:selected { background-color: #e94560; color: white; }"
        "treeview header button { background: #0f3460; color: #00d4aa; font-weight: bold;"
        "                         border-bottom: 2px solid #e94560; }"

        "textview { background-color: #0d0d1a; color: #00d4aa; font-family: monospace; font-size: 12px; }"
        "scrolledwindow { border: 1px solid #0f3460; }"

        "separator { background-color: #e94560; min-height: 1px; }"
        "#status_bar, label { color: #a0a0b0; font-size: 11px; }"
        , -1, NULL);

    gtk_style_context_add_provider_for_screen(
        gdk_screen_get_default(),
        GTK_STYLE_PROVIDER(css),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

    /* Initial data load */
    refresh_memory_list();
    refresh_log_view();

    gtk_widget_show_all(main_window);
    gtk_main();

    log_event("SYSTEM", "SERC Mini-OS GUI Shutdown", -1);
    return 0;
}
