#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>
#include <string.h>
#include <babeltrace2/babeltrace.h>
#include <stdbool.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>
/* This is a ready to be submitted version */
typedef struct custom_event{
    bool last_event;
    char timestamp[32];
    char hostname[32];
    char domain[32];
    char event_name[32];
    uint64_t cpu_id;
    int64_t tid;
    uint64_t payload_size;
    int64_t payloads[0];
} custom_event;

/* Sink component's private data */
struct object_out {
    /* Upstream message iterator (owned by this) */
    bt_message_iterator *message_iterator;
 
    /* Current event message index */
    uint64_t index;

    int sockfd;
};

/* Copied from write.c in sink.text.details component */
static inline
void format_uint(char *buf, uint64_t value, unsigned int base)
{
    const char *spec = "%" PRIu64;
    char *buf_start = buf;

    switch (base) {
    case 2:
    case 16:
        /* TODO: Support binary format */
        spec = "%" PRIx64;
        strcpy(buf, "0x");
        buf_start = buf + 2;
        break;
    case 8:
        spec = "%" PRIo64;
        strcpy(buf, "0");
        buf_start = buf + 1;
        break;
    case 10:
        break;
    default:
        break;
    }

    sprintf(buf_start, spec, value);
}

static inline
void format_int(char *buf, int64_t value, unsigned int base)
{
    const char *spec = "%" PRIu64;
    char *buf_start = buf;
    uint64_t abs_value = value < 0 ? (uint64_t) -value : (uint64_t) value;

    if (value < 0) {
        buf[0] = '-';
        buf_start++;
    }

    switch (base) {
    case 2:
    case 16:
        /* TODO: Support binary format */
        spec = "%" PRIx64;
        strcpy(buf_start, "0x");
        buf_start += 2;
        break;
    case 8:
        spec = "%" PRIo64;
        strcpy(buf_start, "0");
        buf_start++;
        break;
    case 10:
        break;
    default:
        break;
    }

    sprintf(buf_start, spec, abs_value);
}

int64_t generateInt64(int32_t left, int32_t right)
{
  int64_t my_int64 = 0;
  my_int64 = left;
  my_int64 = (my_int64 << 32);
  my_int64 |= right;
//   printf("%lld\n", my_int64);
  return my_int64;
}

void splitInt64ToInt32(int64_t my_int64)
{
  int32_t my_int32_left = 0;
  int32_t my_int32_right = 0;
  my_int32_right = my_int64;
  my_int32_left = my_int64 >> 32;
  printf("%d\n", my_int32_left);
  printf("%d\n", my_int32_right);
}

/*
copy from here
https://stackoverflow.com/questions/20395180/how-to-shift-int64-value-to-unsigned-int64-space-without-changing-the-order
*/
int64_t uint64ToInt64(uint64_t value){
    value ^= numeric_limits<int64_t>::min();
    return *(reinterpret_cast<int64_t*>(&value));
}

/*
int64_t* payload: the final destination for struct
int64_t* content: temp array for char array value
int64_t* non_string_counter: a counter to record non char array type value
int64_t* index: record each char array type value's index in payload

payload{
	prev_comm="swapper/1",
	prev_tid=0,
	prev_prio=20,
	next_comm="lttng-consumerd",
	next_tid=10493
}

so index array would be like:[0,3],
since payload array would store "prev_comm" and "next_comm"'s index(20 and 29) and offset(9 and 15) in index 0 and 3

*/
/*
Question:
1. 怎么把char转成int64_t --- 用对应ASCII转换即可。
2. 遇到float和double类型数据，怎么存储--先直接忽略精度直接存储，后续再说
3. 当前的uint64 转 int64方法是否可行
4. 每个struct的size大小都不一样，接收方怎么对应的进行读取呢？

*/
void write_payload(const bt_field *field, int64_t *payload, int64_t *content, int64_t *non_string_counter, int64_t *string_counter, int64_t *index)
{
    uint64_t i;
    bt_field_class_type fc_type = bt_field_get_class_type(field);
    const bt_field_class *fc;
    char buf[64];

    /* Write field's value */
    if (fc_type == BT_FIELD_CLASS_TYPE_BOOL)
    {
        /*
			BT_TRUE = 1, BT_FALSE = 0
			typedef int bt_bool
		*/
        payload[string_counter + non_string_counter] = (int64_t)bt_field_bool_get_value(field);
        (*non_string_counter)++;
    }
    else if (fc_type == BT_FIELD_CLASS_TYPE_BIT_ARRAY)
    {
        u_int64_t v = bt_field_bit_array_get_value_as_integer(field);
        // convert to int64_t and append.
        payload[string_counter + non_string_counter] = uint64ToInt64(v);
        (*non_string_counter)++;
    }
    else if (bt_field_class_type_is(fc_type,
                                    BT_FIELD_CLASS_TYPE_INTEGER))
    {

        if (bt_field_class_type_is(fc_type,
                                   BT_FIELD_CLASS_TYPE_UNSIGNED_INTEGER))
        {
            u_int64_t v = bt_field_integer_unsigned_get_value(field);
            // convert to int64_t and append.
            payload[string_counter + non_string_counter] = uint64ToInt64(v);
            (*non_string_counter)++;
        }
        else
        {
            payload[string_counter + non_string_counter] = bt_field_integer_signed_get_value(field);
            (*non_string_counter)++;
        }
    }
    else if (fc_type == BT_FIELD_CLASS_TYPE_SINGLE_PRECISION_REAL)
    {
        // get a float, but how to save to int64_t --> drop digit
        float v = bt_field_real_single_precision_get_value(field);
        payload[string_counter + non_string_counter] = (int64_t)v;
        (*non_string_counter)++;
        
    }
    else if (fc_type == BT_FIELD_CLASS_TYPE_DOUBLE_PRECISION_REAL)
    {
        // get a double, how to save it  --> drop digit
        double v = bt_field_real_double_precision_get_value(field);
        payload[string_counter + non_string_counter] = (int64_t)v;
        (*non_string_counter)++;

    }
    else if (fc_type == BT_FIELD_CLASS_TYPE_STRING)
    {
        // get an char pointer
        char *v = bt_field_string_get_value(field);
        int offset = strlen(v);
        int64_t values[offset];
        int i = 0;
        for(; i < offset; i++) 
        {
            values[i] = (int)v[i];
        }
        // copy to content array
        strcpy (content,values);
        // save offset to payloads
        payload[non_string_counter + string_counter] = offset;
        // add to index array--int index[100]
        index[string_counter] = non_string_counter + string_counter;
        // update string counter
        string_counter++;
        // need post operation
    }else if (fc_type == BT_FIELD_CLASS_TYPE_STRUCTURE) {
		uint64_t member_count;

		fc = bt_field_borrow_class_const(field);
		member_count = bt_field_class_structure_get_member_count(fc);

		if (member_count > 0) {
			for (i = 0; i < member_count; i++) {
				const bt_field_class_structure_member *member =
					bt_field_class_structure_borrow_member_by_index_const(
						fc, i);
				const bt_field *member_field =
					bt_field_structure_borrow_member_field_by_index_const(
						field, i);
				write_field(member_field, payload,content,non_string_counter,string_counter,index);
			}
		}
	} else if (bt_field_class_type_is(fc_type, BT_FIELD_CLASS_TYPE_ARRAY)) {
		uint64_t length = bt_field_array_get_length(field);

		for (i = 0; i < length; i++) {
			const bt_field *elem_field =
				bt_field_array_borrow_element_field_by_index_const(
					field, i);
            write_field(elem_field, payload,content,non_string_counter,string_counter,index);
		}
	}else if (bt_field_class_type_is(fc_type,
			BT_FIELD_CLASS_TYPE_OPTION)) {
		const bt_field *content_field =
			bt_field_option_borrow_field_const(field);

		if (content_field) {
            write_field(content_field, payload,content,non_string_counter,string_counter,index);
		}
	} else if (bt_field_class_type_is(fc_type,
			BT_FIELD_CLASS_TYPE_VARIANT)) {
        write_field(bt_field_variant_borrow_selected_option_field_const(
				field), payload,content,non_string_counter,string_counter,index);
	} else {
		bt_common_abort();
	} 
}


/*
 * Initializes the sink component.
 */
static
bt_component_class_initialize_method_status object_csobj_initialize(
        bt_self_component_sink *self_component_sink,
        bt_self_component_sink_configuration *configuration,
        const bt_value *params, void *initialize_method_data)
{
    /* Allocate a private data structure */
    struct object_out *object_out = malloc(sizeof(*object_out));
 
    /* Initialize the first event message's index */
    object_out->index = 1;
 
    /* Set the component's user data to our private data structure */
    bt_self_component_set_data(
        bt_self_component_sink_as_self_component(self_component_sink),
        object_out);
 
    /*
     * Add an input port named `in` to the sink component.
     *
     * This is needed so that this sink component can be connected to a
     * filter or a source component. With a connected upstream
     * component, this sink component can create a message iterator
     * to consume messages.
     */
    bt_self_component_sink_add_input_port(self_component_sink,
        "in", NULL, NULL);

    /*  Initialize the client socket */
    struct sockaddr_in server_addr;
    memset(&server_addr, '0', sizeof(server_addr));
    if((object_out->sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        printf("\n Error : Could not create socket \n");
        return 1;
    }
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(5022);
    if(inet_pton(AF_INET, "127.0.0.1", &server_addr.sin_addr)<=0)
    {
        printf("\n inet_pton error occured\n");
        return 1;
    }
    if(connect(object_out->sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        printf("\n Error: Connect Failed \n");
        return 1;
    }
 
    return BT_COMPONENT_CLASS_INITIALIZE_METHOD_STATUS_OK;
}
 
/*
 * Finalizes the sink component.
 */
static
void object_csobj_finalize(bt_self_component_sink *self_component_sink)
{
    /* Retrieve our private data from the component's user data */
    struct object_out *object_out = bt_self_component_get_data(
        bt_self_component_sink_as_self_component(self_component_sink));
 
    /* Free the allocated structure */
    free(object_out);

    custom_event *custom_event_object = (custom_event*) malloc (sizeof(custom_event));
    custom_event_object->last_event = true;
    send(object_out->sockfd, custom_event_object, sizeof(custom_event), 0);
}
 
/*
 * Called when the trace processing graph containing the sink component
 * is configured.
 *
 * This is where we can create our upstream message iterator.
 */
static
bt_component_class_sink_graph_is_configured_method_status
object_csobj_graph_is_configured(bt_self_component_sink *self_component_sink)
{
    /* Retrieve our private data from the component's user data */
    struct object_out *object_out = bt_self_component_get_data(
        bt_self_component_sink_as_self_component(self_component_sink));
 
    /* Borrow our unique port */
    bt_self_component_port_input *in_port =
        bt_self_component_sink_borrow_input_port_by_index(
            self_component_sink, 0);
 
    /* Create the uptream message iterator */
    bt_message_iterator_create_from_sink_component(self_component_sink,
        in_port, &object_out->message_iterator);
 
    return BT_COMPONENT_CLASS_SINK_GRAPH_IS_CONFIGURED_METHOD_STATUS_OK;
}

static
uint64_t get_uint64_value_from_field(const char *target_name, const bt_field *field, const char *name) {
    bt_field_class_type field_class_type = bt_field_get_class_type(field);
    const bt_field_class *field_class;
    if (bt_field_class_type_is(field_class_type, BT_FIELD_CLASS_TYPE_UNSIGNED_INTEGER)) {
        if (strcmp(target_name, name) == 0) {
            return bt_field_integer_unsigned_get_value(field);
        }
    } else if (field_class_type == BT_FIELD_CLASS_TYPE_STRUCTURE) {
        field_class = bt_field_borrow_class_const(field);
        uint64_t member_count = bt_field_class_structure_get_member_count(field_class);
        for (int i = 0; i < member_count; ++i) {
            const bt_field_class_structure_member *field_class_structure_member =
                bt_field_class_structure_borrow_member_by_index_const(field_class, i);
            const bt_field *member_field =
                bt_field_structure_borrow_member_field_by_index_const(field, i);
            const char *member_name = bt_field_class_structure_member_get_name(field_class_structure_member);
            if (strcmp(target_name, member_name) == 0) {
                return get_uint64_value_from_field(target_name, member_field, member_name);
            }
        }
        /* Error code 999999: target_name not in the structure */
        return 999999;
    } else {
        /* Error code 888888: field is not a structure */
        return 888888;
    }
}

static
int64_t get_int64_value_from_field(const char *target_name, const bt_field *field, const char *name) {
    bt_field_class_type field_class_type = bt_field_get_class_type(field);
    const bt_field_class *field_class;
    if (bt_field_class_type_is(field_class_type, BT_FIELD_CLASS_TYPE_SIGNED_INTEGER)) {
        if (strcmp(target_name, name) == 0) {
            return bt_field_integer_signed_get_value(field);
        }
    } else if (field_class_type == BT_FIELD_CLASS_TYPE_STRUCTURE) {
        field_class = bt_field_borrow_class_const(field);
        uint64_t member_count = bt_field_class_structure_get_member_count(field_class);
        for (int i = 0; i < member_count; ++i) {
            const bt_field_class_structure_member *field_class_structure_member =
                bt_field_class_structure_borrow_member_by_index_const(field_class, i);
            const bt_field *member_field =
                bt_field_structure_borrow_member_field_by_index_const(field, i);
            const char *member_name = bt_field_class_structure_member_get_name(field_class_structure_member);
            if (strcmp(target_name, member_name) == 0) {
                return get_int64_value_from_field(target_name, member_field, member_name);
            }
        }
        /* Error code 999999: target_name not in the structure */
        return 999999;
    } else {
        /* Error code 888888: field is not a structure */
        return 888888;
    }
}

static
const char *get_string_value_from_field(const char *target_name, const bt_field *field, const char *name) {
    bt_field_class_type field_class_type = bt_field_get_class_type(field);
    const bt_field_class *field_class;
    if (field_class_type == BT_FIELD_CLASS_TYPE_STRING) {
        if (strcmp(target_name, name) == 0) {
            return bt_field_string_get_value(field);
        }
    } else if (field_class_type == BT_FIELD_CLASS_TYPE_STRUCTURE) {
        field_class = bt_field_borrow_class_const(field);
        uint64_t member_count = bt_field_class_structure_get_member_count(field_class);
        for (int i = 0; i < member_count; ++i) {
            const bt_field_class_structure_member *field_class_structure_member =
                bt_field_class_structure_borrow_member_by_index_const(field_class, i);
            const bt_field *member_field =
                bt_field_structure_borrow_member_field_by_index_const(field, i);
            const char *member_name = bt_field_class_structure_member_get_name(field_class_structure_member);
            if (strcmp(target_name, member_name) == 0) {
                return get_string_value_from_field(target_name, member_field, member_name);
            }
        }
        /* Error code 999999: target_name not in the structure */
        return "999999";
    } else {
        /* Error code 888888: field is not a structure */
        return "888888";
    }
}

/*
 * Prints a line for `message`, if it's an event message, to the
 * standard csobj.
 */
static
void print_message(struct object_out *object_out, const bt_message *message)
{
    /* Discard if it's not an event message */
    if (bt_message_get_type(message) != BT_MESSAGE_TYPE_EVENT) {
        goto end;
    }
 
    /* Borrow the event message's event and its class */
    const bt_event *event = bt_message_event_borrow_event_const(message);
    const bt_event_class *event_class = bt_event_borrow_class_const(event);
    
    /* Prepare timestamp */
    const bt_clock_snapshot *clock_snapshot = bt_message_event_borrow_default_clock_snapshot_const(message);

    /* Prepare hostname */
	const bt_stream *stream = bt_event_borrow_stream_const(event);
	const bt_trace *trace = bt_stream_borrow_trace_const(stream);
    const bt_value *hostname_value = bt_trace_borrow_environment_entry_value_by_name_const(trace, "hostname");

    /* Prepare domain */
    const bt_value *domain_value = bt_trace_borrow_environment_entry_value_by_name_const(trace, "domain");
	
    /* Prepare the context (aka stream packet context) field members */
    const bt_packet *packet = bt_event_borrow_packet_const(event);
    const bt_field *context_field = bt_packet_borrow_context_field_const(packet);
    
    /* Prepare the common context (aka stream event context) field members */
    const bt_field *common_context_field = bt_event_borrow_common_context_field_const(event);
    
    /* Prepare the payload field members */
    const bt_field *payload_field = bt_event_borrow_payload_field_const(event);
    
    /*
        start here, get payload first, then we can know the size of it and malloc corresponding space
    */

    /*payload array, will be appended to custom_event*/
    int64_t payload[0];
    /*temp array, will be used to store all string type data*/
    char content[0];
    
    int non_string_counter;
    /* counter for string type value, will also be used in index array */
    int string_counter;
    // an array that will store string attributes' index in payload array, will be used in post-operation to get offset and set high 32bits
    int index[100];

    write_field(payload_field, payload,content,&non_string_counter,&string_counter,index);
    // post operation goes here
    strncpy(payload, content);
    int latest_index = string_counter + non_string_counter;
    // set first string type value
    payload[index[0]] = generateInt64(latest_index, payload[index[0]]);
    int i = 1;
    for(; i < string_counter; i++) {
        // calc index
        int offset = index[i - 1];
        latest_index += offset;
        // 对前32位和低32位进行与操作
        payload[index[i]] = generateInt64(latest_index, payload[index[i]]); 
    }
    int payload_size = sizeof(payload);

    /* Create event object to send --> post create */
    int total_size = sizeof(custom_event) + payload_size;
    custom_event *custom_event_object = (custom_event *)malloc(total_size);
    custom_event_object->last_event = false;

    /* Get timestamp */
    int64_t ns_from_origin;
    bt_clock_snapshot_get_ns_from_origin_status cs_status = bt_clock_snapshot_get_ns_from_origin(clock_snapshot, &ns_from_origin);
    if (cs_status == BT_CLOCK_SNAPSHOT_GET_NS_FROM_ORIGIN_STATUS_OK)
    {
        format_int(custom_event_object->timestamp, ns_from_origin, 10);
    }

    /* Get hostname */
    strncpy(custom_event_object->hostname, bt_value_string_get(hostname_value), sizeof(custom_event_object->hostname));

    /* Get domain */
    strncpy(custom_event_object->domain, bt_value_string_get(domain_value), sizeof(custom_event_object->domain));

    /* Get event name */
    strncpy(custom_event_object->event_name, bt_event_class_get_name(event_class), sizeof(custom_event_object->event_name));

    /* Get cpu id */
    custom_event_object->cpu_id = get_uint64_value_from_field("cpu_id", context_field, NULL);
    
    /* Get pid */
    custom_event_object->pid = get_int64_value_from_field("pid", common_context_field, NULL);

    /* set payload */
    strncpy(custom_event_object->payloads, payload, payload_size);
    custom_event_object->payload_size = payload_size;

    /* Send object */
    int converted_size = htonl(total_size);
    // send size first
    send(object_out->sockfd, &converted_size, sizeof(converted_size));
    send(object_out->sockfd, custom_event_object, total_size, 0);

    /* Print index */
    printf("#%" PRIu64, object_out->index);

    printf("\n");
 
    /* Increment the current event message's index */
    object_out->index++;
 
end:
    return;
}
 
/*
 * Consumes a batch of messages and writes the corresponding lines to
 * the standard csobj.
 */
bt_component_class_sink_consume_method_status object_csobj_consume(
        bt_self_component_sink *self_component_sink)
{
    bt_component_class_sink_consume_method_status status =
        BT_COMPONENT_CLASS_SINK_CONSUME_METHOD_STATUS_OK;
 
    /* Retrieve our private data from the component's user data */
    struct object_out *object_out = bt_self_component_get_data(
        bt_self_component_sink_as_self_component(self_component_sink));
 
    /* Consume a batch of messages from the upstream message iterator */
    bt_message_array_const messages;
    uint64_t message_count;
    bt_message_iterator_next_status next_status =
        bt_message_iterator_next(object_out->message_iterator, &messages,
            &message_count);
 
    switch (next_status) {
    case BT_MESSAGE_ITERATOR_NEXT_STATUS_END:
        /* End of iteration: put the message iterator's reference */
        bt_message_iterator_put_ref(object_out->message_iterator);
        status = BT_COMPONENT_CLASS_SINK_CONSUME_METHOD_STATUS_END;
        goto end;
    case BT_MESSAGE_ITERATOR_NEXT_STATUS_AGAIN:
        status = BT_COMPONENT_CLASS_SINK_CONSUME_METHOD_STATUS_AGAIN;
        goto end;
    case BT_MESSAGE_ITERATOR_NEXT_STATUS_MEMORY_ERROR:
        status = BT_COMPONENT_CLASS_SINK_CONSUME_METHOD_STATUS_MEMORY_ERROR;
        goto end;
    case BT_MESSAGE_ITERATOR_NEXT_STATUS_ERROR:
        status = BT_COMPONENT_CLASS_SINK_CONSUME_METHOD_STATUS_ERROR;
        goto end;
    default:
        break;
    }
 
    /* For each consumed message */
    for (uint64_t i = 0; i < message_count; i++) {
        /* Current message */
        const bt_message *message = messages[i];
 
        /* Print line for current message if it's an event message */
        print_message(object_out, message);
 
        /* Put this message's reference */
        bt_message_put_ref(message);
    }
 
end:
    return status;
}
 
/* Mandatory */
BT_PLUGIN_MODULE();
 
/* Define the `object` plugin */
BT_PLUGIN(object);
 
/* Define the `csobj` sink component class */
BT_PLUGIN_SINK_COMPONENT_CLASS(csobj, object_csobj_consume);
 
/* Set some of the `csobj` sink component class's optional methods */
BT_PLUGIN_SINK_COMPONENT_CLASS_INITIALIZE_METHOD(csobj,
    object_csobj_initialize);
BT_PLUGIN_SINK_COMPONENT_CLASS_FINALIZE_METHOD(csobj, object_csobj_finalize);
BT_PLUGIN_SINK_COMPONENT_CLASS_GRAPH_IS_CONFIGURED_METHOD(csobj,
    object_csobj_graph_is_configured);