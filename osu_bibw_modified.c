#define BENCHMARK "OSU MPI%s Bi-Directional Bandwidth Test"
/*
 * Copyright (c) 2002-2024 the Network-Based Computing Laboratory
 * (NBCL), The Ohio State University.
 *
 * Contact: Dr. D. K. Panda (panda@cse.ohio-state.edu)
 *
 * For detailed copyright and licensing information, please refer to the
 * copyright file COPYRIGHT in the top level OMB directory.
 */
#include <osu_util_mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <sys/socket.h>  // Pour socket(), SOCK_DGRAM, etc.
#include <netinet/in.h>  // Pour struct sockaddr_in et AF_INET
#include <arpa/inet.h>   // Pour inet_pton()
#include <unistd.h>      // Pour close()

void chrono(const char *label, double elapsed_time) {
    const char *server_ip = "127.0.0.1"; // Adresse du serveur StatsD
    const int server_port = 8125;         // Port UDP de StatsD
    int udp_socket;
    struct sockaddr_in server_info;

    // Create UDP Socket
    udp_socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (udp_socket < 0) {
        perror("Erreur lors de l'initialisation du socket UDP");
        return;
    }
    // Configure server IP address
    memset(&server_info, 0, sizeof(server_info));
    server_info.sin_family = AF_INET;
    server_info.sin_port = htons(server_port);

    if (inet_pton(AF_INET, server_ip, &server_info.sin_addr) <= 0) {
        perror("Erreur lors de la configuration de l'adresse IP du serveur");
        close(udp_socket);
        return;
    }

    // Prepare and format the message to send
    char data_message[256];
    snprintf(data_message, sizeof(data_message), "mpi_benchmark:%d|g", (int)(elapsed_time * 1000));
    // Send the message to the server
    if (sendto(udp_socket, data_message, strlen(data_message), 0, (struct sockaddr *)&server_info, sizeof(server_info)) < 0) {
        perror("Erreur lors de l'envoi des données au serveur");
    }

    // Close the socket
    close(udp_socket);
}

double calculate_total(double, double, double, int);

int main(int argc, char *argv[])
{
    int myid = 0, numprocs = 0, i = 0, j = 0, k = 0, l = 0;
    int size;
    char **s_buf, **r_buf;
    double t_start = 0.0, t_end = 0.0, t_lo = 0.0, t_total = 0.0;
    int window_size = 64;
    int po_ret = 0;
    int errors = 0;
    double tmp_total = 0.0;
    omb_graph_options_t omb_graph_options;
    omb_graph_data_t *omb_graph_data = NULL;
    int papi_eventset = OMB_PAPI_NULL;
    options.bench = PT2PT;
    options.subtype = BW;
    size_t num_elements = 0;
    MPI_Datatype omb_curr_datatype = MPI_CHAR;
    size_t omb_ddt_transmit_size = 0;
    int mpi_type_itr = 0, mpi_type_size = 0, mpi_type_name_length = 0;
    char mpi_type_name_str[OMB_DATATYPE_STR_MAX_LEN];
    MPI_Datatype mpi_type_list[OMB_NUM_DATATYPES];
    MPI_Comm omb_comm = MPI_COMM_NULL;
    omb_mpi_init_data omb_init_h;
    struct omb_buffer_sizes_t omb_buffer_sizes;
    double *omb_lat_arr = NULL;
    struct omb_stat_t omb_stat;

    set_header(HEADER);
    set_benchmark_name("osu_bibw");

    po_ret = process_options(argc, argv);
    omb_populate_mpi_type_list(mpi_type_list);
    if (PO_OKAY == po_ret && NONE != options.accel) {
        if (init_accel()) {
            fprintf(stderr, "Error initializing device\n");
            exit(EXIT_FAILURE);
        }
    }
    window_size = options.window_size;
    if (options.buf_num == MULTIPLE) {
        s_buf = malloc(sizeof(char *) * window_size);
        r_buf = malloc(sizeof(char *) * window_size);
    } else {
        s_buf = malloc(sizeof(char *) * 1);
        r_buf = malloc(sizeof(char *) * 1);
    }
    if (options.omb_tail_lat) {
        omb_lat_arr = malloc(options.iterations * sizeof(double));
        OMB_CHECK_NULL_AND_EXIT(omb_lat_arr, "Unable to allocate memory");
    }

    omb_init_h = omb_mpi_init(&argc, &argv);
    omb_comm = omb_init_h.omb_comm;
    if (MPI_COMM_NULL == omb_comm) {
        OMB_ERROR_EXIT("Cant create communicator");
    }
    MPI_CHECK(MPI_Comm_rank(omb_comm, &myid));
    MPI_CHECK(MPI_Comm_size(omb_comm, &numprocs));

    omb_graph_options_init(&omb_graph_options);
    if (0 == myid) {
        switch (po_ret) {
            case PO_CUDA_NOT_AVAIL:
                fprintf(stderr, "CUDA support not enabled.  Please recompile "
                                "benchmark with CUDA support.\n");
                break;
            case PO_OPENACC_NOT_AVAIL:
                fprintf(stderr, "OPENACC support not enabled.  Please "
                                "recompile benchmark with OPENACC support.\n");
                break;
            case PO_BAD_USAGE:
                print_bad_usage_message(myid);
                break;
            case PO_HELP_MESSAGE:
                print_help_message(myid);
                break;
            case PO_VERSION_MESSAGE:
                print_version_message(myid);
                omb_mpi_finalize(omb_init_h);
                exit(EXIT_SUCCESS);
            case PO_OKAY:
                break;
        }
    }

    switch (po_ret) {
        case PO_CUDA_NOT_AVAIL:
        case PO_OPENACC_NOT_AVAIL:
        case PO_BAD_USAGE:
            omb_mpi_finalize(omb_init_h);
            exit(EXIT_FAILURE);
        case PO_HELP_MESSAGE:
        case PO_VERSION_MESSAGE:
            omb_mpi_finalize(omb_init_h);
            exit(EXIT_SUCCESS);
        case PO_OKAY:
            break;
    }

    if (numprocs != 2) {
        if (myid == 0) {
            fprintf(stderr, "This test requires exactly two processes\n");
        }

        omb_mpi_finalize(omb_init_h);
        exit(EXIT_FAILURE);
    }

#ifdef _ENABLE_CUDA_
    if (options.src == 'M' || options.dst == 'M') {
        if (options.buf_num == SINGLE) {
            fprintf(stderr, "Warning: Tests involving managed buffers will use"
                            " multiple buffers by default\n");
        }
        options.buf_num = MULTIPLE;
    }
#endif

    if (options.buf_num == SINGLE) {
        if (allocate_memory_pt2pt(&s_buf[0], &r_buf[0], myid)) {
            /* Error allocating memory */
            omb_mpi_finalize(omb_init_h);
            exit(EXIT_FAILURE);
        }
    }

    print_preamble(myid);
    omb_papi_init(&papi_eventset);

    /* Bi-Directional Bandwidth test */
    for (mpi_type_itr = 0; mpi_type_itr < options.omb_dtype_itr;
         mpi_type_itr++) {
        MPI_CHECK(MPI_Type_size(mpi_type_list[mpi_type_itr], &mpi_type_size));
        MPI_CHECK(MPI_Type_get_name(mpi_type_list[mpi_type_itr],
                                    mpi_type_name_str, &mpi_type_name_length));
        omb_curr_datatype = mpi_type_list[mpi_type_itr];
        if (0 == myid) {
            fprintf(stdout, "# Datatype: %s.\n", mpi_type_name_str);
        }
        fflush(stdout);
        print_only_header(myid);
        for (size = options.min_message_size; size <= options.max_message_size;
             size *= 2) {
            num_elements = size / mpi_type_size;
            if (0 == num_elements) {
                continue;
            }
            omb_ddt_transmit_size =
                omb_ddt_assign(&omb_curr_datatype, mpi_type_list[mpi_type_itr],
                               num_elements) *
                mpi_type_size;
            num_elements = omb_ddt_get_size(num_elements);
            if (options.buf_num == MULTIPLE) {
                for (i = 0; i < window_size; i++) {
                    if (allocate_memory_pt2pt_size(&s_buf[i], &r_buf[i], myid,
                                                   size)) {
                        /* Error allocating memory */
                        omb_mpi_finalize(omb_init_h);
                        exit(EXIT_FAILURE);
                    }
                }

                /* touch the data */
                for (i = 0; i < window_size; i++) {
                    set_buffer_pt2pt(s_buf[i], myid, options.accel, 'a', size);
                    set_buffer_pt2pt(r_buf[i], myid, options.accel, 'b', size);
                }
            } else {
                /* touch the data */
                set_buffer_pt2pt(s_buf[0], myid, options.accel, 'a', size);
                set_buffer_pt2pt(r_buf[0], myid, options.accel, 'b', size);
            }

            if (size > LARGE_MESSAGE_SIZE) {
                options.iterations = options.iterations_large;
                options.skip = options.skip_large;
            }
#ifdef _ENABLE_CUDA_KERNEL_
            if ((options.src == 'M' && options.MMsrc == 'D') ||
                (options.dst == 'M' && options.MMdst == 'D')) {
                t_lo = measure_kernel_lo_window(s_buf, size, window_size);
            }
#endif /* #ifdef _ENABLE_CUDA_KERNEL_ */

            omb_graph_allocate_and_get_data_buffer(
                &omb_graph_data, &omb_graph_options, size, options.iterations);
            MPI_CHECK(MPI_Barrier(omb_comm));
            t_total = 0.0;

            for (i = 0; i < options.iterations + options.skip; i++) {
                if (i == options.skip) {
                    omb_papi_start(&papi_eventset);
                }
                if (options.validate) {
                    if (options.buf_num == MULTIPLE) {
                        for (l = 0; l < window_size; l++) {
                            set_buffer_validation(
                                s_buf[l], r_buf[l], size, options.accel, i + l,
                                omb_curr_datatype, omb_buffer_sizes);
                        }
                    } else {
                        set_buffer_validation(
                            s_buf[0], r_buf[0], size, options.accel, i,
                            omb_curr_datatype, omb_buffer_sizes);
                    }
                    MPI_CHECK(MPI_Barrier(omb_comm));
                }
                for (k = 0; k <= options.warmup_validation; k++) {
                    if (myid == 0) {
                        if (i >= options.skip &&
                            k == options.warmup_validation) {
                            t_start = MPI_Wtime();
                        }

#ifdef _ENABLE_CUDA_KERNEL_
                        if (options.src == 'M') {
                            touch_managed_src_window(s_buf, size, window_size,
                                                     ADD);
                        }
#endif /* #ifdef _ENABLE_CUDA_KERNEL_ */

                        for (j = 0; j < window_size; j++) {
                            if (options.buf_num == SINGLE) {
                                MPI_CHECK(MPI_Irecv(
                                    r_buf[0], num_elements, omb_curr_datatype,
                                    1, 10, omb_comm, recv_request + j));
                            } else {
                                MPI_CHECK(MPI_Irecv(
                                    r_buf[j], num_elements, omb_curr_datatype,
                                    1, 10, omb_comm, recv_request + j));
                            }
                        }

                        for (j = 0; j < window_size; j++) {
                            if (options.buf_num == SINGLE) {
                                MPI_CHECK(MPI_Isend(
                                    s_buf[0], num_elements, omb_curr_datatype,
                                    1, 100, omb_comm, send_request + j));
                            } else {
                                MPI_CHECK(MPI_Isend(
                                    s_buf[j], num_elements, omb_curr_datatype,
                                    1, 100, omb_comm, send_request + j));
                            }
                        }

                        MPI_CHECK(
                            MPI_Waitall(window_size, send_request, reqstat));

                        MPI_CHECK(
                            MPI_Waitall(window_size, recv_request, reqstat));

#ifdef _ENABLE_CUDA_KERNEL_
                        if (options.src == 'M') {
                            touch_managed_src_window(r_buf, size, window_size,
                                                     SUB);
                        }
#endif /* #ifdef _ENABLE_CUDA_KERNEL_ */

                        if (i >= options.skip &&
                            k == options.warmup_validation) {
                            t_end = MPI_Wtime();
                            t_total += calculate_total(t_start, t_end, t_lo,
                                                       window_size);
			    // Send t_total to statsd
                            chrono("MPI_Bidirectional_Bandwidth_Test", t_total);

                            if (options.omb_enable_ddt) {
                                tmp_total = omb_ddt_transmit_size / 1e6 *
                                            window_size * 2;
                            } else {
                                tmp_total = size / 1e6 * window_size * 2;
                            }
                            if (options.omb_tail_lat) {
                                omb_lat_arr[i - options.skip] =
                                    tmp_total / calculate_total(t_start, t_end,
                                                                t_lo,
                                                                window_size);
                            }
                            if (options.graph) {
                                omb_graph_data->data[i - options.skip] =
                                    tmp_total / calculate_total(t_start, t_end,
                                                                t_lo,
                                                                window_size);
                            }
                        }
#ifdef _ENABLE_CUDA_KERNEL_
                        if (options.validate &&
                            !(options.src == 'M' && options.MMsrc == 'D' &&
                              options.dst == 'M' && options.MMdst == 'D')) {
                            if (options.src == 'M' && options.MMsrc == 'D') {
                                for (j = 0; j < window_size; j++) {
                                    touch_managed(r_buf[j], size, ADD);
                                    synchronize_stream();
                                }
                            } else if (options.dst == 'M' &&
                                       options.MMdst == 'D') {
                                for (j = 0; j < window_size; j++) {
                                    touch_managed(r_buf[j], size, SUB);
                                    synchronize_stream();
                                }
                            }
                        }
                        if (options.src == 'M' && options.MMsrc == 'D' &&
                            options.validate) {
                            touch_managed_src_window(s_buf, size, window_size,
                                                     SUB);
                        }
#endif /* #ifdef _ENABLE_CUDA_KERNEL_ */
                    } else {
#ifdef _ENABLE_CUDA_KERNEL_
                        if (options.dst == 'M') {
                            touch_managed_dst_window(s_buf, size, window_size,
                                                     ADD);
                        }
#endif /* #ifdef _ENABLE_CUDA_KERNEL_ */

                        for (j = 0; j < window_size; j++) {
                            if (options.buf_num == SINGLE) {
                                MPI_CHECK(MPI_Irecv(
                                    r_buf[0], num_elements, omb_curr_datatype,
                                    0, 100, omb_comm, recv_request + j));
                            } else {
                                MPI_CHECK(MPI_Irecv(
                                    r_buf[j], num_elements, omb_curr_datatype,
                                    0, 100, omb_comm, recv_request + j));
                            }
                        }

                        for (j = 0; j < window_size; j++) {
                            if (options.buf_num == SINGLE) {
                                MPI_CHECK(MPI_Isend(
                                    s_buf[0], num_elements, omb_curr_datatype,
                                    0, 10, omb_comm, send_request + j));
                            } else {
                                MPI_CHECK(MPI_Isend(
                                    s_buf[j], num_elements, omb_curr_datatype,
                                    0, 10, omb_comm, send_request + j));
                            }
                        }

                        MPI_CHECK(
                            MPI_Waitall(window_size, recv_request, reqstat));

#ifdef _ENABLE_CUDA_KERNEL_
                        if (options.dst == 'M') {
                            touch_managed_dst_window(r_buf, size, window_size,
                                                     SUB);
                        }
#endif /* #ifdef _ENABLE_CUDA_KERNEL_ */

                        MPI_CHECK(
                            MPI_Waitall(window_size, send_request, reqstat));
#ifdef _ENABLE_CUDA_KERNEL_
                        if (options.validate &&
                            !(options.src == 'M' && options.MMsrc == 'D' &&
                              options.dst == 'M' && options.MMdst == 'D')) {
                            if (options.src == 'M' && options.MMsrc == 'D') {
                                for (j = 0; j < window_size; j++) {
                                    touch_managed(r_buf[j], size, SUB);
                                    synchronize_stream();
                                }
                            } else if (options.dst == 'M' &&
                                       options.MMdst == 'D') {
                                for (j = 0; j < window_size; j++) {
                                    touch_managed(r_buf[j], size, ADD);
                                    synchronize_stream();
                                }
                            }
                        }
                        if (options.dst == 'M' && options.MMdst == 'D' &&
                            options.validate) {
                            touch_managed_dst_window(s_buf, size, window_size,
                                                     SUB);
                        }
#endif /* #ifdef _ENABLE_CUDA_KERNEL_ */
                    }
                }
                if (i >= options.skip && options.validate) {
                    if (options.buf_num == SINGLE) {
                        errors +=
                            validate_data(r_buf[0], size, 1, options.accel, i,
                                          omb_curr_datatype);
                    } else {
                        for (j = 0; j < window_size; j++) {
                            errors +=
                                validate_data(r_buf[j], size, 1, options.accel,
                                              i + j, omb_curr_datatype);
                        }
                    }
                    if (errors > 0) {
                        break;
                    }
                }
            }
            omb_papi_stop_and_print(&papi_eventset, size);

            if (myid == 0) {
                if (options.omb_enable_ddt) {
                    tmp_total = omb_ddt_transmit_size / 1e6 *
                                options.iterations * window_size * 2;
                } else {
                    tmp_total =
                        size / 1e6 * options.iterations * window_size * 2;
                }
                fprintf(stdout, "%-*d", 10, size);
                if (options.validate) {
                    fprintf(stdout, "%*.*f%*s", FIELD_WIDTH, FLOAT_PRECISION,
                            tmp_total / t_total, FIELD_WIDTH,
                            VALIDATION_STATUS(errors));
                } else {
                    fprintf(stdout, "%*.*f", FIELD_WIDTH, FLOAT_PRECISION,
                            tmp_total / t_total);
                }
                if (options.omb_tail_lat) {
                    omb_stat = omb_calculate_tail_lat(omb_lat_arr, myid, 1);
                    OMB_ITR_PRINT_STAT(omb_stat.res_arr);
                }
                if (options.omb_enable_ddt) {
                    fprintf(stdout, "%*zu", FIELD_WIDTH, omb_ddt_transmit_size);
                }
                fprintf(stdout, "\n");
                fflush(stdout);
                if (options.graph && 0 == myid) {
                    omb_graph_data->avg = tmp_total / t_total;
                }
            }

            omb_ddt_free(&omb_curr_datatype);
            if (options.buf_num == MULTIPLE) {
                for (i = 0; i < window_size; i++) {
                    free_memory(s_buf[i], r_buf[i], myid);
                }
            }

            if (options.validate) {
                MPI_CHECK(MPI_Bcast(&errors, 1, MPI_INT, 0, omb_comm));
                if (0 != errors) {
                    break;
                }
            }
        }
    }
    if (options.graph) {
        omb_graph_plot(&omb_graph_options, benchmark_name);
    }
    omb_graph_combined_plot(&omb_graph_options, benchmark_name);
    omb_graph_free_data_buffers(&omb_graph_options);
    omb_papi_free(&papi_eventset);

    if (options.buf_num == SINGLE) {
        free_memory(s_buf[0], r_buf[0], myid);
    }
    free(s_buf);
    free(r_buf);
    free(omb_lat_arr);
    omb_mpi_finalize(omb_init_h);

    if (NONE != options.accel) {
        if (cleanup_accel()) {
            fprintf(stderr, "Error cleaning up device\n");
            exit(EXIT_FAILURE);
        }
    }
    if (0 != errors && options.validate && 0 == myid) {
        fprintf(stdout,
                "DATA VALIDATION ERROR: %s exited with status %d on"
                " message size %d.\n",
                argv[0], EXIT_FAILURE, size);
        exit(EXIT_FAILURE);
    }

    return EXIT_SUCCESS;
}

double calculate_total(double t_start, double t_end, double t_lo,
                       int window_size)
{
    double t_total;

    if ((options.src == 'M' && options.MMsrc == 'D') &&
        (options.dst == 'M' && options.MMdst == 'D')) {
        t_total = ((t_end - t_start) - (2 * t_lo * window_size));
    } else if ((options.src == 'M' && options.MMsrc == 'D') ||
               (options.dst == 'M' && options.MMdst == 'D')) {
        t_total = ((t_end - t_start) - (t_lo * window_size));
    } else {
        t_total = (t_end - t_start);
    }

    return t_total;
}

