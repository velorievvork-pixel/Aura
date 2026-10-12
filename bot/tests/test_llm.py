"""OpenAI-совместимый клиент на httpx.MockTransport (без сети)."""
import json

import httpx
import pytest

from aura_bot.llm import LLMError, OpenAIChat


def make(handler, **kw):
    return OpenAIChat("k-test", "https://llm.example/v1/", "m", transport=httpx.MockTransport(handler), **kw)


async def test_request_shape_and_answer():
    seen = {}

    def handler(req: httpx.Request):
        seen["url"], seen["auth"], seen["body"] = str(req.url), req.headers["authorization"], json.loads(req.content)
        return httpx.Response(200, json={"choices": [{"message": {"content": "  привет "}}]})

    llm = make(handler)
    assert await llm.complete([{"role": "user", "content": "hi"}], json_mode=True) == "привет"
    assert seen["url"] == "https://llm.example/v1/chat/completions"
    assert seen["auth"] == "Bearer k-test"
    assert seen["body"]["response_format"] == {"type": "json_object"} and seen["body"]["model"] == "m"


async def test_json_mode_can_be_disabled():
    def handler(req):
        assert "response_format" not in json.loads(req.content)
        return httpx.Response(200, json={"choices": [{"message": {"content": "{}"}}]})

    assert await make(handler, json_mode=False).complete([], json_mode=True) == "{}"


@pytest.mark.parametrize("response", [
    httpx.Response(500, text="boom"),
    httpx.Response(200, text="not json"),
    httpx.Response(200, json={"choices": []}),
    httpx.Response(200, json={"choices": [{"message": {"content": ""}}]}),
    httpx.Response(200, json={"choices": [{"message": {"content": None}}]}),
])
async def test_bad_responses_raise(response):
    with pytest.raises(LLMError):
        await make(lambda req: response).complete([])


async def test_network_error_raises():
    def handler(req):
        raise httpx.ConnectError("down")

    with pytest.raises(LLMError):
        await make(handler).complete([])
