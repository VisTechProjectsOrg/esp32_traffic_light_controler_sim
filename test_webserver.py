from fastapi import FastAPI, WebSocket, Request
from fastapi.responses import FileResponse, JSONResponse
from pydantic import BaseModel
import os

app = FastAPI()

# Static folder path
STATIC_DIR = os.path.join(os.path.dirname(__file__), "data")

# Active WebSocket clients
clients = []

# Configuration & state
config = {
    "delay_red": 5,
    "delay_yellow": 2,
    "delay_green": 7,
    "distance_sensor_enabled": True,
    "distance_max": 150,
    "distance_warning": 40,
    "distance_danger": 10,
    "version": "0.1"
}

state = {
    "state": "all_off",
    "light_mode": "cycle_mode",
    "theme_mode": "normal_mode",
    "blink_color": "all",
    "distance_cm": 150
}

# --- WebSocket route ---
@app.websocket("/ws")
async def websocket_endpoint(websocket: WebSocket):
    await websocket.accept()
    clients.append(websocket)
    await websocket.send_json(state)
    try:
        while True:
            await websocket.receive_text()
    except:
        pass
    finally:
        if websocket in clients:
            clients.remove(websocket)

# --- API routes ---
@app.get("/get_config")
async def get_config():
    return JSONResponse(content=config)

@app.post("/set_config")
async def set_config(request: Request):
    data = await request.json()
    config.update(data)
    return {"status": "ok", "updated": config}

@app.get("/get_current_state")
async def get_current_state():
    return JSONResponse(content=state)

@app.get("/toggle_light_mode")
async def toggle_light_mode():
    state["light_mode"] = "blink_mode" if state["light_mode"] == "cycle_mode" else "cycle_mode"
    await broadcast({"light_mode": state["light_mode"]})
    return {"light_mode": state["light_mode"]}

@app.get("/toggle_theme_mode")
async def toggle_theme_mode():
    state["theme_mode"] = "cat_mode" if state["theme_mode"] == "normal_mode" else "normal_mode"
    await broadcast({"theme_mode": state["theme_mode"]})
    return {"theme_mode": state["theme_mode"]}

@app.get("/blink_mode")
async def blink_mode(color: str):
    state["blink_color"] = color
    await broadcast({"blink_color": color})
    return {"blink_color": color}

@app.get("/get_state")
async def get_state():
    return {"state": state["state"]}

# --- NEW: Set traffic light state ---
class StateRequest(BaseModel):
    state: str

@app.post("/set_state")
async def set_state(data: StateRequest):
    valid_states = ["RED", "YELLOW", "GREEN", "OFF"]
    upper_state = data.state.upper()

    if upper_state not in valid_states:
        return JSONResponse({"error": "Invalid state"}, status_code=400)

    state["state"] = upper_state
    await broadcast({"state": upper_state})
    return {"state": upper_state}

# --- Broadcast helper ---
async def broadcast(message: dict):
    for ws in clients:
        try:
            await ws.send_json(message)
        except:
            continue

# --- Static file fallback (like ESP32) ---
@app.get("/{file_path:path}")
async def serve_static(file_path: str):
    full_path = os.path.join(STATIC_DIR, file_path)
    if os.path.isfile(full_path):
        return FileResponse(full_path)
    else:
        return FileResponse(os.path.join(STATIC_DIR, "index.html"))

# --- Run server ---
if __name__ == "__main__":
    import uvicorn
    uvicorn.run("test_webserver:app", host="127.0.0.1", port=8000, reload=True)
