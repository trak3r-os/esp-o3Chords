Import("env")
import os

# Disable IDF Component Manager
env['ENV']['IDF_COMPONENT_MANAGER'] = '0'
os.environ['IDF_COMPONENT_MANAGER'] = '0'

print("IDF Component Manager disabled via environment variable")
