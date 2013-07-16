#include "pebble_os.h"
#include "pebble_app.h"
#include "pebble_fonts.h"

#include "resource_ids.auto.h"

#define MY_NAME "PebbleRSS"
#define MY_UUID { 0x19, 0x41, 0xE6, 0x14, 0x91, 0x63, 0x49, 0xBD, 0xBA, 0x01, 0x6D, 0x7F, 0xA7, 0x1E, 0xED, 0xAC }
PBL_APP_INFO(MY_UUID,
             MY_NAME, "sWp",
             1, 0, /* App version */
             DEFAULT_MENU_ICON,
             APP_INFO_STANDARD_APP);

Window window[3];
MenuLayer menu_layer[2];
ScrollLayer message_layer;
TextLayer messagetext_layer;

#define TITLE_SIZE 128

int currentLevel = 0, selected_item_id = 0, feed_count = 0, item_count = 0;
int feed_receive_idx = 0, item_receive_idx = 0, message_receive_idx = 0;
char feed_names[16][TITLE_SIZE], item_names[128][TITLE_SIZE];
char message[1001];

int in_boost = 0;
void boost() {
  if (!in_boost) {
    app_comm_set_sniff_interval(SNIFF_INTERVAL_REDUCED); // go, go, go  
	in_boost = 1;
  }
}
void throttle() {
  if (in_boost) {
    app_comm_set_sniff_interval(SNIFF_INTERVAL_NORMAL);
	in_boost = 0;
  }
}

void request_command(int slot, int command, bool do_boost) {
  DictionaryIterator *dict;
  app_message_out_get(&dict);
  dict_write_uint8(dict, slot, command);
  dict_write_end(dict); 
  app_message_out_send();
  app_message_out_release();
  if (do_boost) boost();
}

uint16_t menu_get_num_sections_callback(MenuLayer *me, void *data) {
  return 0;
}

uint16_t menu_get_num_rows_callback(MenuLayer *me, uint16_t section_index, void *data) {
  if (currentLevel == 0) // feed
    return feed_count;
  else if (currentLevel == 1) // item
    return item_count;	
  return 0;
}

int16_t menu_get_header_height_callback(MenuLayer *me, uint16_t section_index, void *data) {
  return 0;
}

void menu_draw_header_callback(GContext* ctx, const Layer *cell_layer, uint16_t section_index, void *data) {
}

void menu_draw_row_callback(GContext* ctx, const Layer *cell_layer, MenuIndex *cell_index, void *data) {
  if (currentLevel == 0) // feed
    menu_cell_basic_draw(ctx, cell_layer, feed_names[cell_index->row], NULL, NULL);
  else if (currentLevel == 1) // item
    menu_cell_basic_draw(ctx, cell_layer, item_names[cell_index->row], NULL, NULL);
}

void setup_window(Window *me); // ugh

void menu_select_callback(MenuLayer *me, MenuIndex *cell_index, void *data) {
  if (currentLevel == 0 && feed_count == 0) return;
  if (currentLevel == 1 && item_count == 0) return;
  
  setup_window(&window[++currentLevel]);
  
  if (currentLevel == 1)
    request_command(1091, cell_index->row, true); // get items

  if (currentLevel == 2) {
    selected_item_id = cell_index->row;
    request_command(1092, cell_index->row, true); // get message
  }
}

void message_click(ClickRecognizerRef recognizer, void *context) {
  request_command(1093, selected_item_id, false);
}

void message_click_config_provider(ClickConfig **config, void* context) {
  config[BUTTON_ID_SELECT]->click.handler = message_click;
}

void window_load(Window *me) {
  if (currentLevel < 2) { // feed or item
	menu_layer_init(&menu_layer[currentLevel], me->layer.bounds);		
    menu_layer_set_callbacks(&menu_layer[currentLevel], NULL, (MenuLayerCallbacks){
      .get_num_sections = menu_get_num_sections_callback,
      .get_num_rows = menu_get_num_rows_callback,
      .get_header_height = menu_get_header_height_callback,
      .draw_header = menu_draw_header_callback,
      .draw_row = menu_draw_row_callback,
      .select_click = menu_select_callback,
    }); 
    menu_layer_set_click_config_onto_window(&menu_layer[currentLevel], me);
    layer_add_child(window_get_root_layer(me), menu_layer_get_layer(&menu_layer[currentLevel]));
  }
  else { // message
	message[0] = 0;
	
    text_layer_init(&messagetext_layer, GRect(0, 0, 144, 2048));
	text_layer_set_font(&messagetext_layer, fonts_get_system_font(FONT_KEY_GOTHIC_14));
	text_layer_set_text(&messagetext_layer, (const char*)&message);
	
	scroll_layer_init(&message_layer, me->layer.bounds);
    scroll_layer_add_child(&message_layer, &messagetext_layer.layer);
	scroll_layer_set_click_config_onto_window(&message_layer, me);	
	layer_add_child(window_get_root_layer(me), &messagetext_layer.layer);
	
	window_set_click_config_provider(me, message_click_config_provider);
  }
}

void window_unload(Window *me) {
  if (currentLevel == 1) item_count = 0; //  force reload
  currentLevel--;
}

void setup_window(Window *me) {
  window_init(me, MY_NAME);
  window_set_window_handlers(me, (WindowHandlers){
    .load = window_load,
    .unload = window_unload,
  });
  window_stack_push(me, true);
}

void handle_init(AppContextRef ctx) { 
  resource_init_current_app(&APP_RESOURCES);
  setup_window(&window[0]);
  request_command(1090, 0, true); // hello
}

void handle_deinit(AppContextRef ctx) {
}

void send_ack() {
  request_command(1090, 1, false);
}

void msg_in_rcv_handler(DictionaryIterator *received, void *context) {
  Tuple *feed_tuple = dict_find(received, 1001);
  if (feed_tuple) {
    Tuple *total = dict_find(received, 1011);
    Tuple *offset = dict_find(received, 1012);
	feed_count = total->value->uint8;
	
    memcpy(&feed_names[offset->value->uint8], feed_tuple->value->cstring, feed_tuple->length);
	
	send_ack();
	
	if (++feed_receive_idx == total->value->uint8) { // received all
	  throttle();
	  menu_layer_reload_data(&menu_layer[0]);
	  layer_mark_dirty(menu_layer_get_layer(&menu_layer[0]));
	  feed_receive_idx = 0;	  
	}
  }
  
  Tuple *item_tuple = dict_find(received, 1002);
  if (item_tuple) {
	Tuple *total = dict_find(received, 1011);
	Tuple *offset = dict_find(received, 1012);
	item_count = total->value->uint8;
	
    memcpy(&item_names[offset->value->uint8], item_tuple->value->cstring, item_tuple->length);
	
	send_ack();

	if (++item_receive_idx == total->value->uint8) { // received all
	  throttle();
	  menu_layer_reload_data(&menu_layer[1]);
	  layer_mark_dirty(menu_layer_get_layer(&menu_layer[1]));
	  item_receive_idx = 0;
	}
  }  
  
  Tuple *message_tuple = dict_find(received, 1003);
  if (message_tuple) {
	Tuple *total = dict_find(received, 1011);
	Tuple *offset = dict_find(received, 1012);
	
    memcpy(&message[offset->value->uint16], message_tuple->value->cstring, message_tuple->length);
	
	send_ack();
	
	if (++message_receive_idx == total->value->uint8) {	// received all
	  throttle();
	  scroll_layer_set_content_size(&message_layer, text_layer_get_max_used_size(app_get_current_graphics_context(), &messagetext_layer));
	  layer_mark_dirty(&messagetext_layer.layer); // do NOT invalidate the scroll layer here.
	  message_receive_idx = 0;
	}
  }
}

void msg_in_drp_handler(void *context, AppMessageResult reason) {
}

void pbl_main(void *params) {
  PebbleAppHandlers handlers = {
    .init_handler = &handle_init,
	.deinit_handler = &handle_deinit,
	.messaging_info = {
      .buffer_sizes = {
        .inbound = 256,
        .outbound = 256,
      },
	  .default_callbacks.callbacks = {
        .in_received = msg_in_rcv_handler,
        .in_dropped = msg_in_drp_handler
      }
	}
  };
  app_event_loop(params, &handlers);
}
