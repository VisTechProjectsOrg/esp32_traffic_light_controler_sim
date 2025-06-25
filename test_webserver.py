import http.server
import socketserver
import os
from urllib.parse import parse_qs

PORT = 8000
WEB_DIR = os.path.join(os.path.dirname(__file__), 'data')

current_light_state = "OFF"  # Track the current state

class CustomHandler(http.server.SimpleHTTPRequestHandler):
    def do_GET(self):
        if self.path.startswith("/get_delays"):
            LED_red_delay = 5000
            LED_yellow_delay = 2000
            LED_green_delay = 7000
            distance_sensor_status = "true"
            jsonResponse = (
                '{"red_delay":' + str(LED_red_delay) +
                ',"yellow_delay":' + str(LED_yellow_delay) +
                ',"green_delay":' + str(LED_green_delay) +
                ',"distance_sensor":' + str(distance_sensor_status) + '}'
            )
            self.send_response(200)
            self.send_header('Content-type', 'application/json')
            self.end_headers()
            self.wfile.write(jsonResponse.encode())
        elif self.path.startswith("/get_current_state"):
            jsonResponse = '{"state":"' + current_light_state + '"}'
            self.send_response(200)
            self.send_header('Content-type', 'application/json')
            self.end_headers()
            self.wfile.write(jsonResponse.encode())
        else:
            # Serve static files from /data
            self.path = '/' + self.path.lstrip('/')
            file_path = os.path.join(WEB_DIR, self.path.lstrip('/'))
            if os.path.isdir(file_path):
                file_path = os.path.join(file_path, 'index.html')
            if os.path.exists(file_path):
                with open(file_path, 'rb') as f:
                    self.send_response(200)
                    if file_path.endswith('.html'):
                        self.send_header('Content-type', 'text/html')
                    elif file_path.endswith('.css'):
                        self.send_header('Content-type', 'text/css')
                    elif file_path.endswith('.js'):
                        self.send_header('Content-type', 'application/javascript')
                    elif file_path.endswith('.png'):
                        self.send_header('Content-type', 'image/png')
                    else:
                        self.send_header('Content-type', 'application/octet-stream')
                    self.end_headers()
                    self.wfile.write(f.read())
            else:
                self.send_error(404, "File not found")

    def do_POST(self):
        global current_light_state
        if self.path.startswith("/set_state"):
            content_length = int(self.headers.get('Content-Length', 0))
            post_data = self.rfile.read(content_length).decode()
            params = parse_qs(post_data)
            state = params.get('state', [None])[0]
            if state in ["RED", "YELLOW", "GREEN", "OFF"]:
                current_light_state = state
                response = '{"message":"State changed","state":"' + state + '"}'
                self.send_response(200)
                self.send_header('Content-type', 'application/json')
                self.end_headers()
                self.wfile.write(response.encode())
            else:
                self.send_response(400)
                self.send_header('Content-type', 'application/json')
                self.end_headers()
                self.wfile.write(b'{"error":"Invalid or missing state"}')
        else:
            self.send_response(404)
            self.end_headers()
            self.wfile.write(b'{"error":"Not found"}')

with socketserver.TCPServer(("", PORT), CustomHandler) as httpd:
    print(f"Serving at http://localhost:{PORT}/index.html")
    httpd.serve_forever()