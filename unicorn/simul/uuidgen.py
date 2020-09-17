import uuid

id = uuid.uuid1()
print(f'// {id}')
print('{ ' + ', '.join(map('0x{:02x}'.format, id.bytes)) + ' }')
