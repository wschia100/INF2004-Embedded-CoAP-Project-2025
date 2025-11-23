# Integration Test Plan: CoAP Server (Pico W)

**Version:** 1.1 (Full Command Reference)
**Target System:** Raspberry Pi Pico W CoAP Server
**Protocol:** CoAP over UDP (RFC 7252)
**Tester Client:** `aiocoap` (Python)

---

## 1. Test Environment Setup

Before executing the test cases, ensure the environment matches the following configuration:

* **Device Under Test (DUT):** Raspberry Pi Pico W
* **Firmware:** `microcoap` + Custom Extensions (iPATCH, FETCH)
* **Network Config:**
    * **SSID:** `lomohomo`
    * **Password:** `K0piP3ng`
    * **Static IP:** `192.168.137.50`
    * **Port:** `5683`
* **Client Requirements:** Python 3.x installed with `aiocoap` library.

---

## 2. Test Suite A: Basic Connectivity & Hardware Integration

**Objective:** Verify that the network stack is active and the CoAP layer correctly interfaces with the hardware abstraction layer (HAL).

| ID | Test Case | Command to Execute | Expected Result | Pass/Fail |
|:---|:---|:---|:---|:---|
| **A-01** | **Network Reachability** | `ping 192.168.137.50` | Device responds to ICMP echo requests with low latency. | ☐ |
| **A-02** | **CoAP Port Open** | `nmap -sU -p 5683 192.168.137.50` | Output shows port `5683/udp` is `open` or `open|filtered`. | ☐ |
| **A-03** | **Query Actuators** | `python -m aiocoap.cli.client coap://192.168.137.50:5683/actuators` | Response Payload: `LED=OFF,BUZZER=OFF` (or current state). Status: `2.05 Content`. | ☐ |
| **A-04** | **Query Buttons** | `python -m aiocoap.cli.client coap://192.168.137.50:5683/buttons` | Response Payload: `BTN1=X, BTN2=Y, BTN3=Z` reflecting physical button states. | ☐ |

---

## 3. Test Suite B: Actuator Control (PUT Method)

**Objective:** Verify that external CoAP PUT requests result in physical state changes on the device.

| ID | Test Case | Command to Execute | Expected Result | Pass/Fail |
|:---|:---|:---|:---|:---|
| **B-01** | **LED ON** | `python -m aiocoap.cli.client -m PUT coap://192.168.137.50:5683/actuators --payload "LED=ON"` | Physical LED (GP28) turns **ON**. Response: `2.04 Changed` or `OK`. | ☐ |
| **B-02** | **Buzzer ON** | `python -m aiocoap.cli.client -m PUT coap://192.168.137.50:5683/actuators --payload "BUZZER=ON"` | Physical Buzzer (GP18) emits sound. Response: `2.04 Changed` or `OK`. | ☐ |
| **B-03** | **Combined Control** | `python -m aiocoap.cli.client -m PUT coap://192.168.137.50:5683/actuators --payload "LED=ON,BUZZER=ON"` | Both LED and Buzzer activate simultaneously. | ☐ |
| **B-04** | **Reset State** | `python -m aiocoap.cli.client -m PUT coap://192.168.137.50:5683/actuators --payload "LED=OFF,BUZZER=OFF"` | Both LED and Buzzer turn **OFF**. | ☐ |

---

## 4. Test Suite C: File System & Block Transfer (GET/iPATCH)

**Objective:** Verify read/write access to the SD card via CoAP and handle payloads larger than the MTU (Block-wise transfer).

| ID | Test Case | Command to Execute | Expected Result | Pass/Fail |
|:---|:---|:---|:---|:---|
| **C-01** | **Download Text File** | `python -m aiocoap.cli.client coap://192.168.137.50:5683/file > received.txt` | File `received.txt` is created locally. Content matches server. No block errors. | ☐ |
| **C-02** | **Download Image** | `python -m aiocoap.cli.client "coap://192.168.137.50:5683/file?type=image" > received.jpg` | File `received.jpg` is valid. Can be opened by image viewer. | ☐ |
| **C-03** | **Append Text (iPATCH)** | `python -m aiocoap.cli.client -m iPATCH coap://192.168.137.50:5683/file --payload "Integration Test Line\n"` | Response: `2.04 Changed` (or "Appended"). | ☐ |
| **C-04** | **Verify Append** | `python -m aiocoap.cli.client coap://192.168.137.50:5683/file` | Downloaded file now contains "Integration Test Line" at the end. | ☐ |

---

## 5. Test Suite D: Advanced Data Retrieval (FETCH - RFC 8132)

**Objective:** Verify the custom implementation of the FETCH method for retrieving specific byte/line ranges.

| ID | Test Case | Command to Execute | Expected Result | Pass/Fail |
|:---|:---|:---|:---|:---|
| **D-01** | **Fetch Start Range** | `python -m aiocoap.cli.client -m FETCH coap://192.168.137.50:5683/file --payload "0,4" --content-format 0` | Payload contains **only** lines 0, 1, 2, 3, and 4 of the text file. | ☐ |
| **D-02** | **Fetch Mid Range** | `python -m aiocoap.cli.client -m FETCH coap://192.168.137.50:5683/file --payload "10,15" --content-format 0` | Payload contains **only** lines 10 through 15. | ☐ |
| **D-03** | **Fetch Single Line** | `python -m aiocoap.cli.client -m FETCH coap://192.168.137.50:5683/file --payload "0,0" --content-format 0` | Payload contains exactly 1 line (the first line). | ☐ |

---

## 6. Test Suite E: Negative Testing (Error Handling)

**Objective:** Verify the server gracefully handles invalid inputs without crashing (Stability Testing).

| ID | Test Case | Command to Execute | Expected Response (Error Code) | Pass/Fail |
|:---|:---|:---|:---|:---|
| **E-01** | **FETCH Invalid Range** | `python -m aiocoap.cli.client -m FETCH coap://192.168.137.50:5683/file --payload "10,5" --content-format 0` | `4.00 Bad Request` ("Invalid range") | ☐ |
| **E-02** | **FETCH Negative** | `python -m aiocoap.cli.client -m FETCH coap://192.168.137.50:5683/file --payload "-1,5" --content-format 0` | `4.00 Bad Request` ("Invalid start line") | ☐ |
| **E-03** | **FETCH Missing Fmt** | `python -m aiocoap.cli.client -m FETCH coap://192.168.137.50:5683/file --payload "0,5"` | `4.00 Bad Request` ("Content-Format required") | ☐ |
| **E-04** | **FETCH Wrong Fmt** | `python -m aiocoap.cli.client -m FETCH coap://192.168.137.50:5683/file --payload "0,5" --content-format 60` | `4.15 Unsupported Content-Format` | ☐ |

---

## 7. Test Suite F: Real-time Observation (RFC 7641)

**Objective:** Verify the server can push asynchronous notifications to the client.

* **Setup:** Open a terminal and run the following command to start listening:
  ```bash
  python -m aiocoap.cli.client -m GET coap://192.168.137.50:5683/buttons --observe -vvv
  ```
* **Action:** While the command is running, physically press Button 1, then Button 2, then Button 3 on the hardware.

| ID | Test Case | Action | Expected Result | Pass/Fail |
|:---|:---|:---|:---|:---|
| **F-01** | **Observe Button 1** | Press Button 1 (GP20) | Client receives update packet: `0x42` (or hex equiv). | ☐ |
| **F-02** | **Observe Button 2** | Press Button 2 (GP21) | Client receives update packet string: `BTN1=?,BTN2=1,BTN3=?` | ☐ |
| **F-03** | **Observe Button 3** | Press Button 3 (GP22) | Client receives update packet string: `BTN1=?,BTN2=?,BTN3=1` | ☐ |

---

## 8. Appendix: Quick Reference

### FETCH Logic Matrix
| Condition | Server Response |
|-----------|----------------|
| `start < 0` | `4.00 Bad Request` |
| `end < start` | `4.00 Bad Request` |
| `Buffer overflow` | `4.00 Bad Request` |
| Start > File Length | `2.05 Content` (Empty) |

### Troubleshooting
* **Server unresponsive:** Check USB power and re-run Test **A-01**.
* **Image corrupted:** Ensure `?type=image` is used (Test **C-02**).
* **FETCH 4.00 Error:** Ensure `--content-format 0` is included.
